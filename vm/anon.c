/* anon.c: 디스크 이미지가 아닌 페이지(익명 페이지)의 구현 */

#include "vm/vm.h"
#include "devices/disk.h"
#include "kernel/bitmap.h"
#include "threads/mmu.h"

struct bitmap *swap_table;
struct lock bitmap_lock;
/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1);
	if (swap_disk == NULL){
		PANIC("No swap disk found!");
	}
	swap_table = bitmap_create(disk_size(swap_disk) / 8); 
	if (swap_table == NULL)
		PANIC("Failed to create swap bitmap!");
	lock_init(&bitmap_lock);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	if (page == NULL)
	{
		return false;
	}
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->swap_idx = BITMAP_ERROR;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	
	size_t swap_idx = anon_page->swap_idx;
	if (swap_idx == BITMAP_ERROR) {
		PANIC("swap_in: swap_idx == BITMAP_ERROR. Not swapped out.");
		return false;
	}

	if (!bitmap_test(swap_table, swap_idx)) {
		PANIC("swap_in: bitmap_test failed. Invalid swap_idx.");
		return false;
	}

	for (int i = 0; i < 8; i++) {
		disk_read(swap_disk, swap_idx * 8 + i, kva + i * DISK_SECTOR_SIZE);
	}


	lock_acquire(&bitmap_lock);
	bitmap_set(swap_table, swap_idx, false);
	lock_release(&bitmap_lock);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	lock_acquire(&bitmap_lock);
	size_t swap_idx = bitmap_scan_and_flip(swap_table, 0, 1, false);
	lock_release(&bitmap_lock);
	if (swap_idx == BITMAP_ERROR){
		return false;
	}
	anon_page->swap_idx = swap_idx;

	for (int i = 0; i < 8; i++){
		disk_write(swap_disk, swap_idx * 8 + i, page->frame->kva + DISK_SECTOR_SIZE * i);
	}

	page->frame->page = NULL;
	page->frame = NULL;

	pml4_clear_page(thread_current()->pml4, page->va);
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	
	// 스왑 테이블에서 스왑 인덱스 해제
	if (anon_page->swap_idx != BITMAP_ERROR) {
		lock_acquire(&bitmap_lock);
		bitmap_reset(swap_table, anon_page->swap_idx);
		lock_release(&bitmap_lock);
	}

	// 프레임이 존재하면 프레임을 리스트에서 제거하고 해제
	if (page->frame != NULL) {
		struct frame *f = page->frame;

		if (--f->ref_count == 0) {
			list_remove(&f->elem);
			palloc_free_page(f->kva);
			free(f);
		}
		page->frame = NULL;
	}

	pml4_clear_page(thread_current()->pml4, page->va);

}
