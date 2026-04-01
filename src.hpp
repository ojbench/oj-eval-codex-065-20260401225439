// Block-based int allocator implementation for OJ bench problem 065.
// The allocator obtains memory only via getNewBlock(n) and releases via freeBlock.
// It allocates sequentially from the most recently acquired block (bump-pointer).
// Deallocated space within a block is ignored unless it is the last allocated
// segment in that block (LIFO reuse for the tail), satisfying problem constraints.

#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>
#include <vector>

// External APIs provided by the Online Judge's driver.
int* getNewBlock(int n);
void freeBlock(const int* block, int n);

class Allocator {
public:
    Allocator() = default;

    ~Allocator() {
        // Free all blocks that were obtained but not yet freed.
        for (auto& blk : blocks_) {
            freeBlock(blk.base, blk.blocks_n);
        }
        blocks_.clear();
        alloc_order_.clear();
        ptr_map_.clear();
    }

    int* allocate(int n) {
        if (n <= 0) return nullptr;

        ensure_current_block_capacity(n);

        Block& blk = *current_block_;
        int* ptr = blk.base + blk.used; // contiguous region start

        Segment seg;
        seg.offset = blk.used;
        seg.length = n;
        seg.alive = true;
        blk.segments.push_back(seg);
        blk.used += n;
        blk.active += 1;

        // Record allocation order and map pointer to its metadata
        alloc_order_.push_back({&blk, static_cast<int>(blk.segments.size() - 1)});
        ptr_map_[ptr] = {&blk, static_cast<int>(blk.segments.size() - 1)};

        return ptr;
    }

    void deallocate(int* pointer, int n) {
        if (pointer == nullptr || n <= 0) return;

        auto it = ptr_map_.find(pointer);
        if (it == ptr_map_.end()) {
            // Undefined behavior per problem; ignore gracefully.
            return;
        }

        Block* blk = it->second.block;
        int idx = it->second.segment_index;

        if (idx < 0 || idx >= static_cast<int>(blk->segments.size())) {
            // Inconsistent state; ignore.
            ptr_map_.erase(it);
            return;
        }

        Segment& seg = blk->segments[idx];
        (void)n; // n is guaranteed to match by the judge; we do not enforce.
        if (seg.alive) {
            seg.alive = false;
            blk->active -= 1;
        }
        ptr_map_.erase(it);

        // Try to shrink tail segments for this block (LIFO reuse within block's tail)
        shrink_block_tail(*blk);

        // Also clean up global allocation order tail entries that are already freed
        // and perform cascading tail-shrink on their blocks as necessary.
        cleanup_alloc_order_tail();
    }

private:
    static constexpr int block_size_ints() {
        return static_cast<int>(4096 / sizeof(int));
    }

    struct Segment {
        int offset{0};
        int length{0};
        bool alive{false};
    };

    struct Block {
        int* base{nullptr};
        int blocks_n{0};          // number of 4096-byte blocks backing this Block
        int capacity{0};          // capacity in ints
        int used{0};              // bump-pointer offset (ints)
        int active{0};            // number of alive segments in this Block
        std::vector<Segment> segments; // allocation segments in this Block
    };

    struct Ref {
        Block* block{nullptr};
        int segment_index{-1};
    };

    void ensure_current_block_capacity(int n) {
        if (current_block_ && (current_block_->capacity - current_block_->used) >= n) return;

        // Try to find any existing block with enough tail capacity.
        for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
            if (&(*it) == current_block_) continue;
            int free_tail = it->capacity - it->used;
            if (free_tail >= n) {
                current_block_ = &(*it);
                return;
            }
        }

        // Try to find an empty block we can reuse or free it if too small.
        for (auto it = blocks_.begin(); it != blocks_.end(); ) {
            bool is_empty = (it->active == 0 && it->used == 0 && it->segments.empty());
            if (is_empty) {
                if (it->capacity >= n) {
                    current_block_ = &(*it);
                    return; // Reuse this block
                } else {
                    // Free undersized empty blocks to avoid holding unused memory.
                    freeBlock(it->base, it->blocks_n);
                    bool was_current = (current_block_ == &(*it));
                    it = blocks_.erase(it);
                    if (was_current) current_block_ = nullptr;
                    continue;
                }
            }
            ++it;
        }

        // No suitable reusable block, obtain a new block.
        const int bsz = block_size_ints();
        int needed_blocks = (n + bsz - 1) / bsz; // ceil
        if (needed_blocks <= 0) needed_blocks = 1;
        int* base = getNewBlock(needed_blocks);

        Block newblk;
        newblk.base = base;
        newblk.blocks_n = needed_blocks;
        newblk.capacity = needed_blocks * bsz;
        newblk.used = 0;
        newblk.active = 0;

        blocks_.push_back(newblk);
        current_block_ = &blocks_.back();
    }

    void shrink_block_tail(Block& blk) {
        // Pop freed tail segments and move the used pointer backward accordingly.
        while (!blk.segments.empty() && blk.segments.back().alive == false) {
            const int new_used = blk.segments.back().offset;
            blk.segments.pop_back();
            blk.used = new_used;
        }

        // If the block becomes empty (no used space and no active segments), free it.
        if (blk.active == 0 && blk.used == 0 && blk.segments.empty()) {
            // Locate blk in the list, free and erase it.
            for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
                if (&(*it) == &blk) {
                    freeBlock(it->base, it->blocks_n);
                    // Update current_block_ if needed
                    bool was_current = (current_block_ == &(*it));
                    auto next_it = blocks_.erase(it);
                    if (was_current) {
                        if (next_it == blocks_.end()) {
                            // Set to last block if any, else null
                            current_block_ = blocks_.empty() ? nullptr : &blocks_.back();
                        } else {
                            current_block_ = &(*next_it);
                        }
                    }
                    break;
                }
            }
        }
    }

    void cleanup_alloc_order_tail() {
        // Remove freed allocations from the tail, shrinking their blocks as we go.
        while (!alloc_order_.empty()) {
            Ref& back = alloc_order_.back();
            Block* blk = back.block;
            if (!blk) { alloc_order_.pop_back(); continue; }
            int idx = back.segment_index;
            if (idx < 0 || idx >= static_cast<int>(blk->segments.size())) {
                alloc_order_.pop_back();
                continue;
            }
            if (blk->segments[idx].alive) {
                // Tail is still alive; stop.
                break;
            }
            // Freed: drop from order and shrink that block's tail (if possible)
            alloc_order_.pop_back();
            shrink_block_tail(*blk);
        }
    }

    std::list<Block> blocks_;
    Block* current_block_{nullptr};

    struct MapVal { Block* block; int segment_index; };
    std::unordered_map<int*, MapVal> ptr_map_;
    std::vector<Ref> alloc_order_;
};
