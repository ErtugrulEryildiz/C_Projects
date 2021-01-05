This project is an memory allocator for the x86-64 architecture with the following features:
1. Free lists segregated by size class, using first-fit policy within each size class, augmented with a setof "quick lists" holding small blocks segregated by size.
2. Immediate coalescing of large blocks on free with adjacent free blocks; delayed coalescing on free of small blocks.
3. Boundary tags to support efficient coalescing, with footer optimization that allows footers to be omitted from allocated blocks.
4. Block splitting without creating splinters.
5. Allocated blocks aligned to "double memory row" (16-byte) boundaries.
6. Free lists maintained using last in first out (LIFO) discipline.
7. Obfuscation of block headers and footers to detect heap corruption and attempts to free blocks not previously obtained via allocation.