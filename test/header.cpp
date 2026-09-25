#include "ledger89/admin.h"
int main()
{
    ledger89 *a = nullptr;
    ledger89_admin_report report = {0, 0, 0};
    ledger89_close(a);
    ledger89_admin_check(nullptr, &report); /* -1; forces the admin link */
    return 0;
}
