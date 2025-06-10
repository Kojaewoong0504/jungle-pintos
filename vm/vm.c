/* vm.c: 
 * 가상 메모리(Virtual Memory) 전반에 대한 인터페이스를 제공
 */

#include "threads/malloc.h"
#include "threads/thread.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"

/* 25.05.30 고재웅 작성 */
#include <hash.h>
#include "threads/vaddr.h"
#include "userprog/process.h"

struct lock frame_lock;		/* 25.06.06 정진영 작성 */

/* 각 서브시스템의 초기화 코드를 호출하여 가상 메모리 서브시스템을 초기화합니다. */
void 
vm_init (void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();

	list_init(&frame_table);	/* 25.05.30 고재웅 작성 */
	lock_init(&frame_lock);		/* 25.06.06 정진영 작성 */
}

/* 페이지의 타입을 가져옵니다. 이 함수는 페이지가 초기화된 후 타입을 알고 싶을 때 유용합니다.
 * 이 함수는 이미 완전히 구현되어 있습니다. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);
static struct frame *vm_get_frame(void);
static bool vm_handle_wp (struct page *page UNUSED);


/* 25.06.01 고재웅 작성
 * 초기화 함수와 함께 대기 중인 페이지 객체를 생성한다. 페이지를 직접 생성하지 말고,
 * 반드시 이 함수나 `vm_alloc_page`를 통해 생성하라. 
 * 페이지를 할당하고 타입을 uninit으로 설정한다.
 * */
bool 
vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: 페이지를 생성하고, VM 유형에 따라 초기화 파일을 가져옵니다.Add commentMore actions
		 * TODO: 그런 다음 uninit_new를 호출하여 "uninit" 페이지 구조체를 생성합니다.
		 * TODO: uninit_new를 호출한 후 필드를 수정해야 합니다. */
		struct page *p = malloc(sizeof(struct page));
		if (!p)
			goto err;
		
		typedef bool (*initializer_by_type)(struct page *, enum vm_type, void *);
		initializer_by_type initializer = NULL;

		switch (VM_TYPE(type))
		{
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
			default:
				goto err;
		}
		/* TODO: spt에 페이지를 삽입합니다. */
		uninit_new(p, upage, init, type, aux, initializer);
		p->writable = writable;

		return spt_insert_page(spt, p);
	}
err:
	return false;
}

/* 25.05.30 고재웅 작성
* 25.06.02 고재웅 수정 
* malloc으로 page를 임시 할당해서 사용했는데 free를 해야 하니 제거 하고 지역 변수로 잠깐 사용
* 가상 주소를 통해 SPT에서 페이지를 찾아 리턴한다.
* 에러가 발생하면 NULL을 리턴 */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page key;
	key.va = pg_round_down(va);

	struct hash_elem *e = hash_find(&spt->pages, &key.hash_elem);
	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* 25.05.30 고재웅 작성 
 * 25.05.30 정진영 수정
 * 25.06.01 고재웅 수정
 * PAGE를 spt에 삽입하며 검증을 수행합다. 
 * 가상 주소가 이미 존재하면 삽입하지 않는다. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) 
{
	return hash_insert(&spt->pages, &page->hash_elem) == NULL ? true : false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) 
{	// 아직까지는 불리우는 곳 없는 함수
	hash_delete(&thread_current()->spt.pages, &page->hash_elem);
	vm_dealloc_page (page);
	return true;
}

/* 교체될 struct frame을 가져옵니다. */
static struct frame *
vm_get_victim (void) 
{
	struct frame *victim = NULL;
	/* TODO: 교체 정책을 여기서 구현해서 희생자 페이지 찾기 */

	return victim;
}

/* 한 페이지를 교체(evict)하고 해당 프레임을 반환합니다.
 * 에러가 발생하면 NULL을 반환합니다.*/
static struct frame *
vm_evict_frame (void) 
{
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	/* TODO: 여기서 swap_out 매크로를 호출??
	 *	pml4_clear_page를 아마 사용?? (잘 모름)
	 */
	return NULL;
}

/* 25.05.30 고재웅 작성
 * palloc()을 사용하여 프레임을 할당합니다.
 * 사용 가능한 페이지가 없으면 페이지를 교체(evict)하여 반환합니다.
 * 이 함수는 항상 유효한 주소를 반환합니다. 즉, 사용자 풀 메모리가 가득 차면,
 * 이 함수는 프레임을 교체하여 사용 가능한 메모리 공간을 확보합니다.*/
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	ASSERT (frame != NULL);

	frame->kva = palloc_get_page(PAL_USER | PAL_ZERO);  

	if (frame->kva == NULL)
		frame = vm_evict_frame();
	else
		list_push_back(&frame_table, &frame->elem);

	frame->page = NULL;

	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr)
{
	void *upage = pg_round_down(addr);

	if (spt_find_page(&thread_current()->spt, upage) != NULL)
        return;  // 이미 만들어졌으면 아무 것도 하지 않음

	if (vm_alloc_page(VM_ANON | VM_MARKER_0, upage, 1)) {
		// 스택 최하단에 새 스택 페이지 할당
		bool success = vm_claim_page(upage);

		if (success) {
			if (upage < thread_current()->stack_lower_bound) {
            	thread_current()->stack_lower_bound = upage;
			}
        }
	}
}

/* Handle the fault on write_protected page */
/* Copy on Write 과제용 */
static bool
vm_handle_wp (struct page *page UNUSED) 
{
	return false;
}

/* 25.06.01 고재웅 작성 */
/* Return true on success 
 * 페이지 폴트 핸들러 - 페이지 폴트 발생시 제어권을 전달 받는다.
 * 물리 프레임이 존재하지 않아서 발생한 예외는 not_present 가 true다
 * 그 경우 물리 프레임 할당을 요청하는 vm_do_claim_page를 호출한다.
 * 반대로 not_present 가 false인 경우는 물리 프레임이 할당되어 있지만 폴트가 발생한 것이다.
 * read-only page에 write를 한경우 등 이 때에는 예외 처리를 하면 된다.
 * 그렇다고 해서 not_present가 true인 경우에서 read-only page에 요청을 할 수 있으니 이에
 * 대한 예외를 처리하라
 */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr, bool user, bool write, bool not_present) 
{
	struct thread *cur = thread_current();
	struct supplemental_page_table *spt = &cur->spt;

	// 1. 주소 유효성 검사
	if (addr == NULL || is_kernel_vaddr(addr))
		return false;

	// [TBD] 2. Write-protection fault (present but write to r/o page) 	// 이거 나중에 3번 뒤로 보내야 함
	// if (!not_present && write)
	// // 	return vm_handle_wp(page);
	// 	return false;

	// 3. Stack Growth 케이스
	if (not_present) 
	{
		/* f->rsp: 인터럽트 발생 당시 유저 모드의 %rsp 값 
		 * cur->saved_user_rsp: 커널 모드 스택에서 저장해둔 유저 모드 %rsp 값
		 * 즉, stack growth 판단 시 기준이 되는 rsp는 항상 유저 스택 rsp여야 하기 때문에
		 * user mode/kernel mode fault 인지에 따라 정확한 유저 스택 기준을 얻기 위함
		 */
		void *rsp = user ?  f->rsp : cur->saved_user_rsp;

		/* Case1: PUSH, CALL, INT 등으로 rsp보다 낮은 주소 먼저 접근 */
		/* 스택 포인터보다 더 낮은 주소를 먼저 참조한 경우 */
		if (cur->stack_lower_bound <= rsp - 8 && rsp - 8 == addr && addr <= USER_STACK) {
			vm_stack_growth(addr);
			return true;
		}
		/* Case2: 일반적 stack 사용 */
		else if (cur->stack_lower_bound <= rsp && rsp <= addr && addr <= USER_STACK) {
			vm_stack_growth(addr);
			return true;
		}

		// 4. lazy load 처리
		struct page *page = spt_find_page(spt, addr);	
		if (page == NULL) 
			return false;

		// 5. write 불가능한 페이지에 write 요청한 경우
		if (write && page && !page->writable)
			return false;

		// 6. lazy page 실제 가져오기
		// stack growth 아니고, 기존에 SPT에 등록된 lazy page를 실제로 가져오는 path임
		return vm_do_claim_page(page);
    }
	// 7. not_present == false → 아직 wp 미처리라면 false
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) 
{
	destroy (page);
	free (page);
}

/* 25.06.01 고재웅 수정
 * VA에 해당하는 페이지를 가져온다. 
 * 해당 페이지로 vm_do_claim_page를 호출한다. */
bool
vm_claim_page (void *va) 
{
	struct page *page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL)
		return false;

	return vm_do_claim_page (page);
}

/* 25.06.01 고재웅 수정
 * 인자로 주어진 page에 frame을 할당 한다. --> vm_get_frame()
 * mmu를 설정한다.(pml4) */
static bool
vm_do_claim_page (struct page *page) 
{
	struct frame *frame = vm_get_frame ();
	/* TODO: vm_get_frame이 실패하면 swap_out */
	
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	/* 페이지의 VA와 프레임의 KVA를 페이지 테이블에 매핑 */
	if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
		return false;

	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
/* 25.05.30 고재웅 작성 */

/* 프로세스가 시작될 때(initd) or 포크될 때(__do_fork) 호출되는 함수 */
void
supplemental_page_table_init (struct supplemental_page_table *spt) 
{
	/* SPT 초기화시 hash_init에 아래 작성한 page_hash, page_less를 포함한다. */
	hash_init(spt, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) 
{
	struct hash_iterator i;
	hash_first(&i, &src->pages);
	while (hash_next(&i))
	{
		// src_page 정보
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		enum vm_type type = src_page->operations->type;		// 현재 페이지는 어떤 타입인지 (VM_UNINIT, VM_ANON, VM_FILE 중 하나)
		void *upage = src_page->va;
		bool writable = src_page->writable;

		/* 1) type이 uninit이면 */
		if (VM_TYPE(type) == VM_UNINIT)
		{ 	
			enum vm_type real_type = VM_TYPE(src_page->uninit.type);	// 지금은 아직 UNINIT지만, 나중에 page fault가 터지면 어떤 타입으로 쓸 건지
			vm_initializer *init = src_page->uninit.init;
			void *aux = src_page->uninit.aux;

			/* page fault 시 lazy load 를 위해 필요 */
			if (!vm_alloc_page_with_initializer(real_type, upage, writable, init, aux))
				return false;

			continue;
		}

		/* 2) type이 file이면{ */
		if (VM_TYPE(type) == VM_FILE) 
		{	
			struct lazy_load_arg *aux = malloc(sizeof(struct lazy_load_arg));
            aux->file = src_page->file.file;
            aux->ofs = src_page->file.ofs;
            aux->read_bytes = src_page->file.read_bytes;
            aux->zero_bytes = src_page->file.zero_bytes;

			/* file_backed_initializer 자동 호출되므로 init은 NULL, aux는 file 읽기 정보 위해 필요 */
            if (!vm_alloc_page_with_initializer(VM_TYPE(type), upage, writable, NULL, aux))
                return false;

            // struct page *page = spt_find_page(dst, upage);
            // file_backed_initializer(page, type, NULL);
            // page->frame = src_page->frame;
            // pml4_set_page(thread_current()->pml4, page->va, src_page->frame->kva, src_page->writable);
            continue;
		}

		/* 3) type이 uninit이 아니면 바로 claim 해서 쓰므로 필요 없음 */
		if (!vm_alloc_page_with_initializer(VM_TYPE(type), upage, writable, NULL, NULL)) // uninit page 생성 & 초기화
			// init(lazy_load_segment)는 page_fault가 발생할때 호출됨
			// 지금 만드는 페이지는 page_fault가 일어날 때까지 기다리지 않고 바로 내용을 넣어줘야 하므로 필요 없음
			return false;

		// vm_claim_page으로 요청해서 매핑 & 페이지 타입에 맞게 초기화
		if (!vm_claim_page(upage))
			return false;

		// 매핑된 프레임에 내용 로딩
		struct page *dst_page = spt_find_page(dst, upage);
		memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
	}
	return true;
}

// bool
// supplemental_page_table_copy (struct supplemental_page_table *dst,
//                               struct supplemental_page_table *src) 
// {
//     struct hash_iterator i;
//     hash_first(&i, &src->pages);
//     while (hash_next(&i))
//     {
//         struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
//         enum vm_type type = src_page->operations->type;

//         // ⭐ 실제 type 결정 (VM_UNINIT이면 미래 type 사용)
//         enum vm_type real_type = (type == VM_UNINIT) ? VM_TYPE(src_page->uninit.type) : VM_TYPE(type);

//         void *upage = src_page->va;
//         bool writable = src_page->writable;

//         // 1️⃣ ANON 처리
//         if (real_type == VM_ANON)
//         {
//             vm_initializer *init = NULL;
//             void *aux = NULL;

//             // UNINIT이면 기존 init, aux 복사
//             if (type == VM_UNINIT)
//             {
//                 init = src_page->uninit.init;
//                 aux = src_page->uninit.aux;
//             }

//             if (!vm_alloc_page_with_initializer(real_type, upage, writable, init, aux))
//                 return false;

//             // claim 후 내용 복사
//             if (!vm_claim_page(upage))
//                 return false;

//             struct page *dst_page = spt_find_page(dst, upage);
//             memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
//             continue;
//         }

//         // 2️⃣ FILE 처리
//         else if (real_type == VM_FILE)
//         {
//             struct lazy_load_arg *aux = malloc(sizeof(struct lazy_load_arg));
//             if (aux == NULL)
//                 return false;

//             // src_page에서 file 관련 정보 복사
//             aux->file = src_page->file.file;
//             aux->ofs = src_page->file.ofs;
//             aux->read_bytes = src_page->file.read_bytes;
//             aux->zero_bytes = src_page->file.zero_bytes;

//             if (!vm_alloc_page_with_initializer(real_type, upage, writable, NULL, aux))
//             {
//                 free(aux);
//                 return false;
//             }

//             // ⭐ FILE page는 claim 하면 안 됨 (lazy load 유지해야 함)
//             continue;
//         }

//         // 3️⃣ 나머지 타입 처리: ex) VM_MARKER_x 가 붙은 ANON page 등
//         else
//         {
//             if (!vm_alloc_page_with_initializer(real_type, upage, writable, NULL, NULL))
// 				return false;

// 			if (!vm_claim_page(upage))
// 				return false;

// 			struct page *dst_page = spt_find_page(dst, upage);
// 			memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);

// 			continue;
//         }
//     }
//     return true;
// }


/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) 
{
	/* TODO: 스레드가 보유한 모든 supplemental_page_table을 제거하고,
	 * TODO: 수정된 내용을 스토리지에 기록(writeback)하세요. */
	hash_clear(&spt->pages, hash_page_destroy);
}

/* 25.05.30 고재웅 작성 */
/* SPT 해시 테이블에 넣기 위한 hash_func & less_func 함수 구현 */

/* page_hash 가상 주소를 바탕으로 해시값을 계산한다. */
uint64_t page_hash(const struct hash_elem *e, void *aux){
	struct page *p = hash_entry(e, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

/* 두 page의 va를 기준으로 정렬을 비교한다. */
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux){
	/* 이 함수는 해시 테이블 충돌 시 내부 정렬에 사용된다고 한다. */
	struct page *pa = hash_entry(a, struct page, hash_elem);
	struct page *pb = hash_entry(b, struct page, hash_elem);
	return pa->va < pb->va;
}

void hash_page_destroy(struct hash_elem *e, void *aux)
{
	struct page *page = hash_entry(e, struct page, hash_elem);
	destroy(page);
	free(page);
}
