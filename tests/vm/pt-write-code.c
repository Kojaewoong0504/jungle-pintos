/* Try to write to the code segment.
   The process must be terminated with -1 exit code. */
   
/* 코드 영역 (read-only)에 쓰기 접근 시, 이를 막아야 성공 */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  *(int *) test_main = 0;
  fail ("writing the code segment succeeded");
}
