/* MessageBoxW implementation for user32.dll.
 * Returns IDOK (1) in headless mode, or shows a dialog if display available.
 * This is a minimal implementation for testing builtin module functionality.
 */
typedef unsigned int UINT;
__attribute__((ms_abi)) int MessageBoxW(void *hWnd, const uint16_t *lpText, const uint16_t *lpCaption, UINT uType)
{
    (void)hWnd; (void)lpText; (void)lpCaption; (void)uType;
    /* TODO: Implement proper message box with display */
    return 1; /* IDOK */
}