// Block-based int allocator implementation for OJ bench problem 065.
// The allocator obtains memory only via getNewBlock(n) and releases via freeBlock.
// It allocates sequentially from the most recently acquired block (bump-pointer).
// Deallocated space within a block is ignored unless it is the last allocated
// segment in that block (LIFO reuse for the tail), satisfying problem constraints.

#pragma once

#include <cstddef>
#include <list>

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
        int* ptr = blk.base + blk.used;
        blk.used += n;
        blk.active += 1;
        return ptr;
    }

    void deallocate(int* pointer, int n) {
        if (pointer == nullptr || n <= 0) return;

        Block* blk = find_block(pointer);
        if (!blk) return;

        (void)n; // guaranteed to match
        // Prefer this block for subsequent allocations to satisfy reuse expectation
        current_block_ = blk;

        // If deallocating the tail-most allocation of this block, bump used backward
        std::ptrdiff_t off = pointer - blk->base; // ints offset
        if (off + n == blk->used) {
            blk->used = static_cast<int>(off);
        }
        if (blk->active > 0) blk->active -= 1;

        // If this block becomes empty, free it immediately
        if (blk->active == 0 && blk->used == 0) {
            // erase and free
            for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
                if (&(*it) == blk) {
                    freeBlock(it->base, it->blocks_n);
                    bool was_current = (current_block_ == &(*it));
                    auto next_it = blocks_.erase(it);
                    if (was_current) {
                        current_block_ = (next_it == blocks_.end()) ? (blocks_.empty() ? nullptr : &blocks_.back()) : &(*next_it);
                    }
                    break;
                }
            }
        }
    }

private:
    static constexpr int block_size_ints() {
        return static_cast<int>(4096 / sizeof(int));
    }

    struct Block {
        int* base{nullptr};
        int blocks_n{0};          // number of 4096-byte blocks backing this Block
        int capacity{0};          // capacity in ints
        int used{0};              // bump-pointer offset (ints)
        int active{0};            // number of alive segments in this Block
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
            bool is_empty = (it->active == 0 && it->used == 0);
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

    std::list<Block> blocks_;
    Block* current_block_{nullptr};

    Block* find_block(const int* p) {
        for (auto& b : blocks_) {
            if (p >= b.base && p < b.base + b.capacity) return &b;
        }
        return nullptr;
    }
};
