namespace factor
{

template<typename Block> struct free_list_allocator {
	cell size;
	cell start;
	cell end;
	cell high_water_mark;
	free_list free_blocks;
	mark_bits<Block> state;

	explicit free_list_allocator(cell size, cell start);
	void initial_free_list(cell occupied);
	bool contains_p(Block *block);
	Block *first_block();
	Block *last_block();
	Block *next_block_after(Block *block);
	Block *next_allocated_block_after(Block *block);
	bool can_allot_p(cell size);
	Block *allot(cell size);
	void free(Block *block);
	cell occupied_space();
	cell free_space();
	cell largest_free_block();
	cell free_block_count();
	void sweep();
	template<typename Iterator> void sweep(Iterator &iter);
	template<typename Iterator, typename Sizer> void compact(Iterator &iter, Sizer &sizer);
	template<typename Iterator, typename Sizer> void iterate(Iterator &iter, Sizer &sizer);
	template<typename Iterator> void iterate(Iterator &iter);
};

template<typename Block>
free_list_allocator<Block>::free_list_allocator(cell size_, cell start_) :
	size(size_),
	start(start_),
	end(start_ + size_),
	state(mark_bits<Block>(size_,start_))
{
	initial_free_list(0);
}

template<typename Block> void free_list_allocator<Block>::initial_free_list(cell occupied)
{
	free_blocks.initial_free_list(start,end,occupied);
	high_water_mark = free_blocks.free_space;
}

template<typename Block> bool free_list_allocator<Block>::contains_p(Block *block)
{
	return ((cell)block - start) < size;
}

template<typename Block> Block *free_list_allocator<Block>::first_block()
{
	return (Block *)start;
}

template<typename Block> Block *free_list_allocator<Block>::last_block()
{
	return (Block *)end;
}

template<typename Block> Block *free_list_allocator<Block>::next_block_after(Block *block)
{
	return (Block *)((cell)block + block->size());
}

template<typename Block> Block *free_list_allocator<Block>::next_allocated_block_after(Block *block)
{
	while(block != this->last_block() && block->free_p())
	{
		free_heap_block *free_block = (free_heap_block *)block;
		block = (object *)((cell)free_block + free_block->size());
	}

	if(block == this->last_block())
		return NULL;
	else
		return block;
}

template<typename Block> bool free_list_allocator<Block>::can_allot_p(cell size)
{
	return free_blocks.can_allot_p(size);
}

template<typename Block> Block *free_list_allocator<Block>::allot(cell size)
{
	size = align(size,block_granularity);

	free_heap_block *block = free_blocks.find_free_block(size);
	if(block)
	{
		block = free_blocks.split_free_block(block,size);
		return (Block *)block;
	}
	else
		return NULL;
}

template<typename Block> void free_list_allocator<Block>::free(Block *block)
{
	free_heap_block *free_block = (free_heap_block *)block;
	free_block->make_free(block->size());
	free_blocks.add_to_free_list(free_block);
}

template<typename Block> cell free_list_allocator<Block>::free_space()
{
	return free_blocks.free_space;
}

template<typename Block> cell free_list_allocator<Block>::occupied_space()
{
	return size - free_blocks.free_space;
}

template<typename Block> cell free_list_allocator<Block>::largest_free_block()
{
	return free_blocks.largest_free_block();
}

template<typename Block> cell free_list_allocator<Block>::free_block_count()
{
	return free_blocks.free_block_count;
}

template<typename Block>
void free_list_allocator<Block>::sweep()
{
	free_blocks.clear_free_list();

	Block *prev = NULL;
	Block *scan = this->first_block();
	Block *end = this->last_block();

	while(scan != end)
	{
		cell size = scan->size();

		if(scan->free_p())
		{
			if(prev && prev->free_p())
			{
				free_heap_block *free_prev = (free_heap_block *)prev;
				free_prev->make_free(free_prev->size() + size);
			}
			else
				prev = scan;
		}
		else if(this->state.marked_p(scan))
		{
			if(prev && prev->free_p())
				free_blocks.add_to_free_list((free_heap_block *)prev);
			prev = scan;
		}
		else
		{
			if(prev && prev->free_p())
			{
				free_heap_block *free_prev = (free_heap_block *)prev;
				free_prev->make_free(free_prev->size() + size);
			}
			else
			{
				free_heap_block *free_block = (free_heap_block *)scan;
				free_block->make_free(size);
				prev = scan;
			}
		}

		scan = (Block *)((cell)scan + size);
	}

	if(prev && prev->free_p())
		free_blocks.add_to_free_list((free_heap_block *)prev);

	high_water_mark = free_blocks.free_space;
}

template<typename Block>
template<typename Iterator>
void free_list_allocator<Block>::sweep(Iterator &iter)
{
	free_blocks.clear_free_list();

	Block *prev = NULL;
	Block *scan = this->first_block();
	Block *end = this->last_block();

	while(scan != end)
	{
		cell size = scan->size();

		if(scan->free_p())
		{
			if(prev && prev->free_p())
			{
				free_heap_block *free_prev = (free_heap_block *)prev;
				free_prev->make_free(free_prev->size() + size);
			}
			else
				prev = scan;
		}
		else if(this->state.marked_p(scan))
		{
			if(prev && prev->free_p())
				free_blocks.add_to_free_list((free_heap_block *)prev);
			prev = scan;
			iter(scan,size);
		}
		else
		{
			if(prev && prev->free_p())
			{
				free_heap_block *free_prev = (free_heap_block *)prev;
				free_prev->make_free(free_prev->size() + size);
			}
			else
			{
				free_heap_block *free_block = (free_heap_block *)scan;
				free_block->make_free(size);
				prev = scan;
			}
		}

		scan = (Block *)((cell)scan + size);
	}

	if(prev && prev->free_p())
		free_blocks.add_to_free_list((free_heap_block *)prev);

	high_water_mark = free_blocks.free_space;
}

template<typename Block, typename Iterator> struct heap_compactor {
	mark_bits<Block> *state;
	char *address;
	Iterator &iter;

	explicit heap_compactor(mark_bits<Block> *state_, Block *address_, Iterator &iter_) :
		state(state_), address((char *)address_), iter(iter_) {}

	void operator()(Block *block, cell size)
	{
		if(this->state->marked_p(block))
		{
			iter(block,(Block *)address,size);
			address += size;
		}
	}
};

/* The forwarding map must be computed first by calling
state.compute_forwarding(). */
template<typename Block>
template<typename Iterator, typename Sizer>
void free_list_allocator<Block>::compact(Iterator &iter, Sizer &sizer)
{
	heap_compactor<Block,Iterator> compactor(&state,first_block(),iter);
	iterate(compactor,sizer);

	/* Now update the free list; there will be a single free block at
	the end */
	free_blocks.initial_free_list(start,end,(cell)compactor.address - start);
}

/* During compaction we have to be careful and measure object sizes differently */
template<typename Block>
template<typename Iterator, typename Sizer>
void free_list_allocator<Block>::iterate(Iterator &iter, Sizer &sizer)
{
	Block *scan = first_block();
	Block *end = last_block();

	while(scan != end)
	{
		cell size = sizer(scan);
		Block *next = (Block *)((cell)scan + size);
		if(!scan->free_p()) iter(scan,size);
		scan = next;
	}
}

template<typename Block> struct standard_sizer {
	cell operator()(Block *block)
	{
		return block->size();
	}
};

template<typename Block>
template<typename Iterator>
void free_list_allocator<Block>::iterate(Iterator &iter)
{
	standard_sizer<Block> sizer;
	iterate(iter,sizer);
}

}