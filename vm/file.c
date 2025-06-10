/* file.c: Implementation of memory backed file object (mmaped object). */

#include <stdlib.h>
#include "vm/vm.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/file.h"

struct lock filesys_lock;

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) 
{
	/* 전역 자료구조 초기화 */
	/** TODO: mmap 리스트 초기화
	 * mmap으로 만들어진 페이지를 리스트로 관리하면
	 * munmmap 시에 더 빠르게 할 수 있을 듯 합니다
	 *
	 */
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	// TODO: page에 page->uninit.aux에 들어있는 정보를 구조체로 형변환 (ex. lazy_load_arg *)

	struct lazy_load_arg *aux = (struct lazy_load_arg *)page->uninit.aux;
	file_page->file = aux->file;
	file_page->ofs = aux->ofs;
	file_page->read_bytes = aux->read_bytes;
	file_page->zero_bytes = aux->zero_bytes;
	return true;
}

/* 파일에서 내용을 읽어와 페이지를 스왑인합니다. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
	// swap_in을 위한 버퍼
	void *buffer[PGSIZE];
	/** TODO: 파일에서 정보를 읽어와 kva에 복사하세요
	 * aux에 저장된 백업 정보를 사용하세요
	 * file_open과 read를 사용하면 될 것 같아요
	 * 파일 시스템 동기화가 필요할수도 있어요
	 * 필요시 file_backed_initializer를 수정하세요
	 */
}

/* 페이지의 내용을 파일에 기록(writeback)하여 스왑아웃합니다. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
	/** TODO: dirty bit 확인해서 write back
	 * pml4_is_dirty를 사용해서 dirty bit를 확인하세요
	 * write back을 할 때는 aux에 저장된 파일 정보를 사용
	 * file_write를 사용하면 될 것 같아요
	 * dirty_bit 초기화 (pml4_set_dirty)
	 */
}

/* 파일 기반 페이지를 소멸시킵니다. PAGE는 호출자가 해제합니다. */
static void
file_backed_destroy(struct page *page)
{
	struct file_page *file_page = &page->file;

	 // 해당 페이지가 dirty 상태인지 확인
	 if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	 }
	 pml4_clear_page(thread_current()->pml4, page->va);
}

// struct lazy_load_arg *
// make_arg(struct file *file, off_t ofs, size_t read_bytes)
// {
// 	struct lazy_load_info *arg = malloc(sizeof(struct lazy_load_arg));
// 	if (!arg) return NULL;

// 	arg->file = file;
// 	arg->ofs = ofs;
// 	arg->read_bytes = read_bytes;
// 	arg->zero_bytes = (read_bytes < PGSIZE) ? PGSIZE - read_bytes : 0;

// 	return arg;
// }

/* do_mmap(): mmap 로직 전체를 책임 (페이지 등록, lazy loading, file 재열기 포함) 
 * 
 * lazy loading으로 할당
 * 파일에 대한 독립적인 참조
 * 여러 페이지에 매핑되는 파일 처리
 */
void *
do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset)
{
	void *start_addr = addr;				// 성공 시 반환할 원래 가상 주소
	int mmap_idx = 0;  						// 각 페이지에 부여할 mmap_idx

	// 파일 재열기 → 독립된 file 객체 필요 (close(fd)와 무관하게 파일 내용을 유지)
	// 파일 시스템 작업이므로 동기화를 위해 전역 filesys_lock 획득 필요
	lock_acquire(&filesys_lock);			
	struct file *reopened_file = file_reopen(file);	
	lock_release(&filesys_lock);			

	if (reopened_file == NULL) return NULL;

	// 실제로 읽어야 할 바이트 수: 요청한 길이와 파일 크기 중 더 작은 값
	// 페이지 보정용 zero padding 계산
	size_t read_bytes = (file_length(reopened_file) < length) ? file_length(reopened_file) : length;
	size_t zero_bytes = (read_bytes % PGSIZE == 0) ? 0 : PGSIZE - (read_bytes % PGSIZE);
	
	// 전체 매핑 범위에 이미 매핑된 페이지가 있는지 충돌 검사
	for (void *check = addr; check < addr + read_bytes + zero_bytes; check += PGSIZE) {
		if (spt_find_page(&thread_current()->spt, check) != NULL)
			return NULL;
	}

	// 정렬 조건 검사
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(addr) == 0);
	ASSERT(offset % PGSIZE == 0);

	// 페이지 단위로 매핑 수행
	size_t total_bytes = read_bytes + zero_bytes;
	while (total_bytes > 0) 
	{
		// 현재 페이지에서 실제로 읽을 바이트 수와 남은 공간 zero padding 계산
		size_t page_read_bytes = (read_bytes < PGSIZE) ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		// lazy load용 인자 구조체 생성
		struct lazy_load_arg *aux = malloc(sizeof(struct lazy_load_arg));
		if (aux == NULL) {
			file_close(reopened_file);
			return NULL;
		}

		aux->file = reopened_file;
		aux->ofs = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;

		// lazy load로 VM_FILE 페이지 등록
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, aux)) 
		{
			free(aux);
			file_close(reopened_file);
			return NULL;
		}

		// 해당 주소에 실제로 할당된 page 구조체를 가져와 mmap_idx 저장
		struct page *p = spt_find_page(&thread_current()->spt, addr);
		if (p != NULL)
			p->mmap_idx = mmap_idx++;

		// 다음 페이지로 이동
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}	

	return start_addr;
}

/** TODO: mmap으로 매핑된 모든 페이지를 없애야함
 * 1. SPT에서 제거
 * 2. 물리 페이지에서도 제거
 * 3. 매핑 카운트나 page 구조체 내의 카운트를 사용해서 제거
 */
void do_munmap(void *addr) 
{
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page;

	lock_acquire(&filesys_lock);		// 파일 시스템과의 동기화를 위해 전역 락 획득 (파일과 관련된 페이지일 수 있기 때문)	

	page = spt_find_page(spt, addr);	// 현재 주소에 해당하는 페이지를 보조 페이지 테이블에서 검색
	if (page == NULL) {					// 페이지가 존재하지 않으면 즉시 반환 (락 해제 후)
		lock_release(&filesys_lock);
		return;
	}

	int mmap_idx = page->mmap_idx;		// 이 페이지가 속한 mmap 영역의 전체 페이지 수를 가져옴

	// mmap_idx 만큼 반복하며 페이지 해제 수행
	for (int i = 0; i < mmap_idx; i++) {
		struct page *target = spt_find_page(spt, addr);		// 현재 주소에 해당하는 페이지를 다시 찾음
		if (target != NULL)				// 페이지가 존재한다면 보조 페이지 테이블에서 제거 (즉, unmap)
			// destroy(target_page);
			spt_remove_page(spt, target);					
		addr += PGSIZE;					// 다음 페이지 주소로 이동 (페이지 크기만큼 증가)
	}

	lock_release(&filesys_lock);		// 락 해제 (모든 페이지 제거 후)
}
