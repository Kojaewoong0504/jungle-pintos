/* vm.h: 
 * 가상 메모리(Virtual Memory) 시스템이 지원해야 하는 각기 다른 vm_type(가상 메모리 타입)들 
 * -- VM_UNINIT, VM_ANON, VM_FILE, VM_PAGE_CACHE -- 의 정의와 설명
 */

#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"
#include <hash.h>	/* Project 3: Memory Management */

enum vm_type {
	/* 초기화되지 않은 페이지 */
	VM_UNINIT = 0,
	/* 파일과 관련 없는 페이지, 즉 익명 페이지 */
	VM_ANON = 1,
	/* 파일과 관련된 페이지 */
	VM_FILE = 2,
	/* 페이지 캐시를 보유하는 페이지, for project 4 */
	VM_PAGE_CACHE = 3,
	/* mmap 파일 페이지 */
	VM_MMAP = 4,

	/* 상태를 저장하기 위한 비트 플래그 */

	/* 추가 정보를 저장하기 위한 보조 비트 플래그 마커입니다.
	 * int 범위 내에서 값을 추가할 수 있습니다. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* 이 값을 초과하지 마세요. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;
struct list frame_table;

#define VM_TYPE(type) ((type) & 7)

/* 25.06.04 정진영 수정 (mmap_idx 추가) */
/* "페이지" 표현
 * 이는 일종의 "부모 클래스"이며, 네 개의 "자식 클래스"를 갖습니다.
 * uninit_page, file_page, anon_page, 그리고 page cache(project4)입니다.
 * 이 구조체의 미리 정의된 멤버를 제거하거나 수정하지 마십시오. */
struct page {
	const struct page_operations *operations;	/* 페이지에 대한 동작 함수 (swap-in, swap-out 등) */
	void *va;              						/* 사용자 주소 공간의 가상 주소 */
	struct frame *frame;   						/* 이 페이지가 매핑된 물리 프레임 */

	/* Your implementation */
	/* 25.05.30 고재웅 작성 */
	struct hash_elem hash_elem;			// 해시 저장용 elem
	bool writable; 						// 쓰기 가능한 페이지 인지
	bool is_loaded;						// 실제로 프레임에 로드되어 있는지
	int mmap_idx;						// mmap 전체 중 몇번째 페이지인지를 기록

	/* union은 여러 타입 중 하나만을 저장할 수 있는 특수한 자료형으로,  
	 * 타입별 데이터는 union에 바인딩 됩니다. 각 함수는 현재 union을 자동으로 감지합니다. */
	union {
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* The representation of "frame" */
struct frame {
	void *kva;
	struct page *page;
	struct list_elem elem;	/* 25.05.30 고재웅 작성 */
};

/* 페이지 작업을 위한 함수 테이블입니다.
 * 이는 C에서 "인터페이스"를 구현하는 한 가지 방법입니다.
 * "메서드"의 테이블을 구조체의 멤버에 넣고,
 * 필요할 때마다 호출하면 됩니다. */
struct page_operations {
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};

/* lazy load 시 사용되는 argument 구조체 */
struct lazy_load_arg {
    struct file *file;
    off_t ofs;
    uint32_t read_bytes;
    uint32_t zero_bytes;
};

/* lazy load 시 사용되는 argument 구조체 */
// struct lazy_load_arg {
//     enum vm_type type;
//     union {
//         struct {
//             struct file *file;
//             off_t ofs;
//             uint32_t read_bytes;
//             uint32_t zero_bytes;
//         } file_arg;
//         struct {
//             struct file *file;
//             off_t ofs;
//             uint32_t read_bytes;
//             uint32_t zero_bytes;
//         } anon_arg;
//     };
// };

#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)

/* 현재 프로세스의 메모리 공간을 나타냅니다.
 * 이 구조체에 대해 특정 설계를 강제하지 않습니다.
 * 모든 설계는 여러분에게 달려 있습니다. */
struct supplemental_page_table {
	struct hash pages;		/* 25.05.30 고재웅 작성 */
};

#include "threads/thread.h"
void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);
struct page *spt_find_page (struct supplemental_page_table *spt,
		void *va);
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);

void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
		bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);

/* 25.05.30 고재웅 작성 */
/* SPT 해시 테이블에 넣기 위한 hash_func & less_func 함수 선언 */
uint64_t page_hash(const struct hash_elem *e, void *aux);
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux);
void hash_page_destroy(struct hash_elem *e, void *aux);

/* 25.06.06 정진영 작성 */
#define STACK_LIMIT (USER_STACK - (1 << 20))

#endif  /* VM_VM_H */
