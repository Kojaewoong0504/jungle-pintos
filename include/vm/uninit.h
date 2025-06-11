/* uninit.h:
 * 접근 시점까지 로딩을 지연하는 초기화되지 않은 페이지를 정의합니다.
 * 가상 메모리 시스템에서 Lazy Loading을 지원하기 위해 사용됩니다.
 */

#ifndef VM_UNINIT_H
#define VM_UNINIT_H
#include "vm/vm.h"

struct page;
enum vm_type;

typedef bool vm_initializer (struct page *, void *aux);

/* Uninitlialized page. The type for implementing the
 * "Lazy loading". */
struct uninit_page {
	/* Initiate the contets of the page */
	vm_initializer *init;
	enum vm_type type;			/* 페이지 폴트 시 초기화 될 페이지 타입 */
	void *aux;
	/* Initiate the struct page and maps the pa to the va */
	bool (*page_initializer) (struct page *, enum vm_type, void *kva);
};

void uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *kva));
#endif
