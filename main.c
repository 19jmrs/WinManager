#ifndef UNICODE
#define UNICODE
#endif 

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// Structure to hold window information for the tabs controller
typedef struct {
    HWND hwnd;
    wchar_t title[256];
    wchar_t className[256];
} WindowInfo;

// Global variables for tabs controller
static WindowInfo* g_windows = NULL;
static int g_windowCount = 0;
static int g_selectedIndex = 0;
static BOOL g_showingTabs = FALSE;
static HWND g_mainHwnd = NULL;
static BOOL g_orderInitialized = FALSE;
static HWND g_previouslyFocusedWindow = NULL; // Store the window that was focused before showing overlay

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);
void ShowTabsOverlay(HWND hwnd);
void HideTabsOverlay(HWND hwnd);
void DrawTabsList(HDC hdc, RECT* rect);
void FocusSelectedWindow();
void SwapWindows(int index1, int index2);
void UpdateWindowList();
void SaveWindowOrder();
void LoadWindowOrder();
int FindWindowInList(HWND hwnd);
BOOL IsValidWindow(HWND hwnd);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    // Register the window class.
    const wchar_t CLASS_NAME[]  = L"TabsController";
    
    WNDCLASS wc = { };

    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    // Create the window.
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,  // Always on top, don't show in taskbar, layered for transparency
        CLASS_NAME,                         // Window class
        L"Tabs Controller",                 // Window text
        WS_POPUP,                           // Popup window without border

        // Size and position (will be adjusted when showing)
        0, 0, 400, 300,

        NULL,       // Parent window    
        NULL,       // Menu
        hInstance,  // Instance handle
        NULL        // Additional application data
        );

    if (hwnd == NULL)
    {
        return 0;
    }

    g_mainHwnd = hwnd;

    // Load previously saved window order
    LoadWindowOrder();

    // Register global hotkey: Shift + Tab (VK_TAB with MOD_SHIFT)
    if (!RegisterHotKey(hwnd, 1, MOD_SHIFT, VK_TAB))
    {
        MessageBox(NULL, L"Failed to register hotkey Shift+Tab", L"Error", MB_OK);
    }

    // Don't show the window initially
    // ShowWindow(hwnd, nCmdShow);

    // Run the message loop.
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    SaveWindowOrder();
    if (g_windows) {
        free(g_windows);
    }
    UnregisterHotKey(hwnd, 1);

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_HOTKEY:
        // Handle Shift+Tab hotkey press
        if (wParam == 1) {
            if (!g_showingTabs) {
                ShowTabsOverlay(hwnd);
            } else {
                HideTabsOverlay(hwnd);
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (g_showingTabs) {
            // Check if Ctrl key is pressed
            BOOL ctrlPressed = GetKeyState(VK_CONTROL) & 0x8000;
            
            // Debug: Show which key was pressed
            wchar_t debugMsg[100];
            if (ctrlPressed) {
                wsprintfW(debugMsg, L"Ctrl+Key: %d, Selected: %d/%d (Reorder Mode)", wParam, g_selectedIndex, g_windowCount);
            } else {
                wsprintfW(debugMsg, L"Key: %d, Selected: %d/%d", wParam, g_selectedIndex, g_windowCount);
            }
            SetWindowTextW(hwnd, debugMsg);
            
            switch (wParam) {
            case VK_UP:
                if (ctrlPressed) {
                    // Reorder: Move selected item up
                    if (g_selectedIndex > 0) {
                        SwapWindows(g_selectedIndex, g_selectedIndex - 1);
                        g_selectedIndex--;
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                } else {
                    // Navigate up in the list
                    if (g_selectedIndex > 0) {
                        g_selectedIndex--;
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                }
                return 0;
            case VK_DOWN:
                if (ctrlPressed) {
                    // Reorder: Move selected item down
                    if (g_selectedIndex < g_windowCount - 1) {
                        SwapWindows(g_selectedIndex, g_selectedIndex + 1);
                        g_selectedIndex++;
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                } else {
                    // Navigate down in the list
                    if (g_selectedIndex < g_windowCount - 1) {
                        g_selectedIndex++;
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                }
                return 0;
            case VK_RETURN:
                // Focus the selected window
                FocusSelectedWindow();
                HideTabsOverlay(hwnd);
                return 0;
            case VK_ESCAPE:
                // Hide the overlay without selecting
                HideTabsOverlay(hwnd);
                return 0;
            // Handle number keys 1-9 for direct window selection
            case '1': case '2': case '3': case '4': case '5':
            case '6': case '7': case '8': case '9':
                {
                    int windowIndex = wParam - '1'; // Convert '1' to 0, '2' to 1, etc.
                    if (windowIndex >= 0 && windowIndex < g_windowCount) {
                        g_selectedIndex = windowIndex;
                        FocusSelectedWindow();
                        HideTabsOverlay(hwnd);
                    }
                }
                return 0;
            }
        }
        break;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            if (g_showingTabs) {
                DrawTabsList(hdc, &ps.rcPaint);
            } else {
                // Fill with background color when not showing tabs
                FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW+1));
            }

            EndPaint(hwnd, &ps);
        }
        return 0;

    case WM_ACTIVATE:
        // Hide tabs overlay if window loses focus
        if (LOWORD(wParam) == WA_INACTIVE && g_showingTabs) {
            HideTabsOverlay(hwnd);
        }
        break;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Callback function to enumerate windows and filter valid application windows
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    if (!IsValidWindow(hwnd)) {
        return TRUE; // Continue enumeration
    }

    // Reallocate memory for the new window
    g_windows = (WindowInfo*)realloc(g_windows, (g_windowCount + 1) * sizeof(WindowInfo));
    if (!g_windows) {
        return FALSE; // Stop enumeration on memory error
    }

    // Get window title
    GetWindowTextW(hwnd, g_windows[g_windowCount].title, 256);
    
    // Get window class name
    GetClassNameW(hwnd, g_windows[g_windowCount].className, 256);
    
    // Store window handle
    g_windows[g_windowCount].hwnd = hwnd;
    
    g_windowCount++;
    return TRUE; // Continue enumeration
}

// Check if a window is a valid application window to display in tabs
BOOL IsValidWindow(HWND hwnd)
{
    if (!IsWindowVisible(hwnd)) return FALSE;
    if (hwnd == g_mainHwnd) return FALSE; // Don't include our own window
    
    // Check if window has a title
    wchar_t title[256];
    int titleLen = GetWindowTextW(hwnd, title, 256);
    if (titleLen == 0) return FALSE;
    
    // Get window class name for filtering
    wchar_t className[256];
    GetClassNameW(hwnd, className, 256);
    
    // Filter out common system/background applications by title
    const wchar_t* excludedTitles[] = {
        L"RZMonitorForegroundWindow",
        L"Definições",
        L"NVIDIA GeForce Overlay",
        L"Program Manager",
        L"Desktop Window Manager",
        L"Windows Security",
        L"Action Center",
        L"Microsoft Text Input Application",
        L"Windows Input Experience",
        L"Cortana",
        L"Search",
        L"Windows Shell Experience Host",
        L"Background Task Host",
        NULL
    };
    
    // Check if title matches any excluded pattern
    for (int i = 0; excludedTitles[i] != NULL; i++) {
        if (wcsstr(title, excludedTitles[i]) != NULL) {
            return FALSE;
        }
    }
    
    // Special allowlist for applications that should always be included
    const wchar_t* allowedTitles[] = {
        L"Steam",
        L"Discord",
        L"Spotify",
        L"Chrome",
        L"Firefox",
        L"Visual Studio",
        L"Code",
        L"Notepad",
        L"Explorer",
        NULL
    };
    
    // Check if this is an explicitly allowed application
    BOOL isAllowed = FALSE;
    for (int i = 0; allowedTitles[i] != NULL; i++) {
        if (wcsstr(title, allowedTitles[i]) != NULL) {
            isAllowed = TRUE;
            break;
        }
    }
    
    // If it's an allowed app, skip most other filtering
    if (isAllowed) {
        // Still check basic visibility and parent
        HWND parent = GetParent(hwnd);
        if (parent != NULL && parent != GetDesktopWindow()) return FALSE;
        return TRUE;
    }
    
    // Filter out common system window classes
    const wchar_t* excludedClasses[] = {
        L"Shell_TrayWnd",           // Taskbar
        L"Shell_SecondaryTrayWnd",  // Secondary taskbar
        L"Progman",                 // Desktop
        L"WorkerW",                 // Desktop worker
        L"DV2ControlHost",          // Windows UI
        L"Windows.UI.Core.CoreWindow", // UWP system windows
        L"ApplicationFrameWindow",  // Some UWP apps
        L"Windows.UI.Composition.DesktopWindowContentBridge", // Windows UI
        L"ForegroundStaging",       // Windows system
        L"MultitaskingViewFrame",   // Task view
        L"EdgeUiInputTopWndClass",  // Edge UI
        L"NativeHWNDHost",          // Windows system
        L"Shell_InputSwitchTopLevelWindow", // Input method
        L"Windows.Internal.CapturePicker", // Capture picker
        L"XamlExplorerHostIslandWindow", // Windows Explorer
        L"CortanaUI",               // Cortana
        L"SearchUI",                // Windows Search
        NULL
    };
    
    // Check if class name matches any excluded pattern
    for (int i = 0; excludedClasses[i] != NULL; i++) {
        if (wcsstr(className, excludedClasses[i]) != NULL) {
            return FALSE;
        }
    }
    
    // Check window styles to filter out non-application windows
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    
    // Skip tool windows but be more lenient for actual applications
    if (exStyle & WS_EX_TOOLWINDOW) {
        // Skip known system tool windows
        if (wcsstr(className, L"Shell_") || wcsstr(className, L"DV2ControlHost")) {
            return FALSE;
        }
        // For other tool windows, check if they're reasonably sized applications
        RECT rect;
        if (GetWindowRect(hwnd, &rect)) {
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            // Allow larger tool windows (they might be legitimate apps)
            if (width < 200 || height < 100) {
                return FALSE;
            }
        }
        // If we can't get the size, but it's not a known system class, allow it
    }
    
    if (!(style & WS_VISIBLE)) return FALSE;
    
    // Check if window has a parent (skip child windows)
    HWND parent = GetParent(hwnd);
    if (parent != NULL && parent != GetDesktopWindow()) return FALSE;
    
    // Additional check: must have WS_CAPTION or be a popup with reasonable size
    if (!(style & WS_CAPTION) && !(style & WS_POPUP)) {
        // Exception: Some applications like Steam might not have standard captions
        // but still be valid applications. Check if it has a reasonable size.
        RECT rect;
        if (GetWindowRect(hwnd, &rect)) {
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            if (width < 200 || height < 100) {
                return FALSE; // Too small to be a main application window
            }
        } else {
            return FALSE; // Can't get rect, likely not a valid app window
        }
    }
    
    // Filter out windows that are likely system overlays (very small or positioned off-screen)
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        
        // Skip very small windows (likely system indicators) - but be more lenient
        if (width < 100 || height < 50) return FALSE;
        
        // Skip windows positioned far off-screen (likely hidden system windows)
        if (rect.left < -1000 || rect.top < -1000) return FALSE;
    }
    
    return TRUE;
}

// Show the tabs overlay with list of applications
void ShowTabsOverlay(HWND hwnd)
{
    // Store the currently focused window before showing our overlay
    g_previouslyFocusedWindow = GetForegroundWindow();
    
    g_selectedIndex = 0;

    // Update window list while preserving custom order
    UpdateWindowList();

    if (g_windowCount == 0) {
        MessageBox(NULL, L"No windows found", L"Info", MB_OK);
        return; // No windows to show
    }

    // Calculate window size based on number of items
    int itemHeight = 30;
    int padding = 20;
    int width = 600;
    int height = (g_windowCount * itemHeight) + (padding * 2);
    
    // Limit height to screen size
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    if (height > screenHeight - 100) {
        height = screenHeight - 100;
    }
    
    // Center the window on screen
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int x = (screenWidth - width) / 2;
    int y = (screenHeight - height) / 2;

    // Resize and position the window
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    
    // Set transparency: 220 out of 255 (about 86% opacity, 14% transparent)
    SetLayeredWindowAttributes(hwnd, 0, 220, LWA_ALPHA);
    
    g_showingTabs = TRUE;
    
    // Force focus to our window for keyboard input
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    
    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
}

// Hide the tabs overlay
void HideTabsOverlay(HWND hwnd)
{
    g_showingTabs = FALSE;
    ShowWindow(hwnd, SW_HIDE);
}

// Draw the list of tabs/applications
void DrawTabsList(HDC hdc, RECT* rect)
{
    if (!g_windows || g_windowCount == 0) return;

    // Set up drawing
    SetBkMode(hdc, TRANSPARENT);
    HFONT hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    // Fill background with a dark gray color
    HBRUSH hBrush = CreateSolidBrush(RGB(59, 69, 79));
    FillRect(hdc, rect, hBrush);
    DeleteObject(hBrush);

    // Draw a subtle rounded border effect
    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(80, 80, 80));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    SelectObject(hdc, hPen);                        
    // Draw rounded rectangle effect
    RECT borderRect = *rect;
    borderRect.left += 1;
    borderRect.top += 1;
    borderRect.right -= 1;
    borderRect.bottom -= 1;
    RoundRect(hdc, borderRect.left, borderRect.top, borderRect.right, borderRect.bottom, 10, 10);

    // Draw each window item
    int itemHeight = 30;
    int padding = 10;
    
    // Add debug info
    wchar_t debugText[150];
wsprintfW(debugText, L"Found %d windows - Blue: selected, Green: active, ●: currently focused, Reorder with Ctrl+Arrows", g_windowCount);
    RECT debugRect = {rect->left + 5, rect->top + 5, rect->right - 5, rect->top + 25};
    SetTextColor(hdc, RGB(0, 0, 0));
    DrawTextW(hdc, debugText, -1, &debugRect, DT_LEFT | DT_TOP | DT_SINGLELINE);
    
    for (int i = 0; i < g_windowCount; i++) {
        RECT itemRect;
        itemRect.left = rect->left + padding;
        itemRect.right = rect->right - padding;
        itemRect.top = rect->top + padding + 25 + (i * itemHeight); // +25 for debug text
        itemRect.bottom = itemRect.top + itemHeight;

        // Skip if item is outside visible area
        if (itemRect.top >= rect->bottom) break;

        // Highlight selected item
        if (i == g_selectedIndex) {
            HBRUSH hSelBrush = CreateSolidBrush(RGB(30, 3, 200)); // Steel blue background
            FillRect(hdc, &itemRect, hSelBrush);
            DeleteObject(hSelBrush);
            SetTextColor(hdc, RGB(255, 255, 255)); // White text for selected item
        } else {
            // Check if this is the currently focused window for special highlighting
            // Use the previously focused window instead of current foreground (which is our overlay)
            BOOL isCurrentlyFocused = (g_windows[i].hwnd == g_previouslyFocusedWindow);
            
            if (isCurrentlyFocused) {
                // Highlight currently focused window with a different color
                HBRUSH hFocusBrush = CreateSolidBrush(RGB(60, 120, 60)); // Dark green background
                FillRect(hdc, &itemRect, hFocusBrush);
                DeleteObject(hFocusBrush);
                SetTextColor(hdc, RGB(144, 238, 144)); // Light green text
            } else {
                SetTextColor(hdc, RGB(0, 0, 0)); // Light gray text for normal items
            }
        }

        // Draw window title with index number and reorder indicators
        wchar_t displayText[350];
        
        // Check if this is the currently focused window
        // Use the previously focused window instead of current foreground (which is our overlay)
        BOOL isCurrentlyFocused = (g_windows[i].hwnd == g_previouslyFocusedWindow);
        
        if (i < 9) { // Only show numbers for first 9 items
            // Check if Ctrl is currently pressed for visual feedback
            BOOL ctrlPressed = GetKeyState(VK_CONTROL) & 0x8000;
            if (ctrlPressed && i == g_selectedIndex) {
                if (isCurrentlyFocused) {
                    wsprintfW(displayText, L"[%d] ● ↕ %s (REORDER - ACTIVE)", i + 1, g_windows[i].title);
                } else {
                    wsprintfW(displayText, L"[%d] ↕ %s (REORDER)", i + 1, g_windows[i].title);
                }
            } else {
                if (isCurrentlyFocused) {
                    wsprintfW(displayText, L"[%d] ● %s (ACTIVE)", i + 1, g_windows[i].title);
                } else {
                    wsprintfW(displayText, L"[%d] %s", i + 1, g_windows[i].title);
                }
            }
        } else {
            if (isCurrentlyFocused) {
                wsprintfW(displayText, L"    ● %s (ACTIVE)", g_windows[i].title);
            } else {
                wsprintfW(displayText, L"    %s", g_windows[i].title);
            }
        }
        DrawTextW(hdc, displayText, -1, &itemRect, 
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // Cleanup
    SelectObject(hdc, hOldFont);
    SelectObject(hdc, hOldPen);
    DeleteObject(hFont);
    DeleteObject(hPen);
}

// Focus the currently selected window
void FocusSelectedWindow()
{
    if (!g_windows || g_selectedIndex < 0 || g_selectedIndex >= g_windowCount) {
        return;
    }

    HWND targetHwnd = g_windows[g_selectedIndex].hwnd;
    
    // Check if window is still valid
    if (!IsWindow(targetHwnd)) {
        return;
    }

    // Restore window if minimized
    if (IsIconic(targetHwnd)) {
        ShowWindow(targetHwnd, SW_RESTORE);
    }

    // Bring window to foreground
    SetForegroundWindow(targetHwnd);
    SetFocus(targetHwnd);
}

// Swap two windows in the list for reordering functionality
void SwapWindows(int index1, int index2)
{
    if (!g_windows || index1 < 0 || index2 < 0 || 
        index1 >= g_windowCount || index2 >= g_windowCount || 
        index1 == index2) {
        return;
    }

    // Swap the WindowInfo structures
    WindowInfo temp = g_windows[index1];
    g_windows[index1] = g_windows[index2];
    g_windows[index2] = temp;
}

// Find a window in the current list by HWND
int FindWindowInList(HWND hwnd)
{
    for (int i = 0; i < g_windowCount; i++) {
        if (g_windows[i].hwnd == hwnd) {
            return i;
        }
    }
    return -1; // Not found
}

// Update window list while preserving custom order
void UpdateWindowList()
{
    // Create a temporary list of all current valid windows
    WindowInfo* tempWindows = NULL;
    int tempCount = 0;
    
    // Enumerate all current windows
    HWND hwnd = GetTopWindow(NULL);
    while (hwnd != NULL) {
        if (IsValidWindow(hwnd)) {
            // Reallocate temp array
            tempWindows = (WindowInfo*)realloc(tempWindows, (tempCount + 1) * sizeof(WindowInfo));
            if (!tempWindows) break;
            
            // Store window info
            tempWindows[tempCount].hwnd = hwnd;
            GetWindowTextW(hwnd, tempWindows[tempCount].title, 256);
            GetClassNameW(hwnd, tempWindows[tempCount].className, 256);
            tempCount++;
        }
        hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
    }
    
    if (!g_orderInitialized || !g_windows) {
        // First time or no previous data - just use the temp list
        if (g_windows) free(g_windows);
        g_windows = tempWindows;
        g_windowCount = tempCount;
        g_orderInitialized = TRUE;
        return;
    }
    
    // Create new list preserving order
    WindowInfo* newWindows = NULL;
    int newCount = 0;
    
    // First, add existing windows in their current order
    for (int i = 0; i < g_windowCount; i++) {
        // Check if this window still exists
        BOOL stillExists = FALSE;
        for (int j = 0; j < tempCount; j++) {
            if (tempWindows[j].hwnd == g_windows[i].hwnd) {
                stillExists = TRUE;
                break;
            }
        }
        
        if (stillExists && IsWindow(g_windows[i].hwnd)) {
            // Add to new list
            newWindows = (WindowInfo*)realloc(newWindows, (newCount + 1) * sizeof(WindowInfo));
            if (!newWindows) break;
            
            newWindows[newCount] = g_windows[i];
            // Update title in case it changed
            GetWindowTextW(g_windows[i].hwnd, newWindows[newCount].title, 256);
            newCount++;
        }
    }
    
    // Then, add any new windows at the end
    for (int i = 0; i < tempCount; i++) {
        BOOL alreadyInList = FALSE;
        for (int j = 0; j < newCount; j++) {
            if (newWindows[j].hwnd == tempWindows[i].hwnd) {
                alreadyInList = TRUE;
                break;
            }
        }
        
        if (!alreadyInList) {
            // Add new window to end
            newWindows = (WindowInfo*)realloc(newWindows, (newCount + 1) * sizeof(WindowInfo));
            if (!newWindows) break;
            
            newWindows[newCount] = tempWindows[i];
            newCount++;
        }
    }
    
    // Replace old list with new one
    if (g_windows) free(g_windows);
    g_windows = newWindows;
    g_windowCount = newCount;
    
    // Clean up temp list
    if (tempWindows) free(tempWindows);
}

// Save window order to a simple text file
void SaveWindowOrder()
{
    if (!g_windows || g_windowCount == 0) return;
    
    FILE* file = fopen("winmanager_order.txt", "w");
    if (!file) return;
    
    fprintf(file, "%d\n", g_windowCount);
    for (int i = 0; i < g_windowCount; i++) {
        // Save window handle and title (for identification)
        fprintf(file, "%p|%ls\n", (void*)g_windows[i].hwnd, g_windows[i].title);
    }
    
    fclose(file);
}

// Load window order from file
void LoadWindowOrder()
{
    FILE* file = fopen("winmanager_order.txt", "r");
    if (!file) return;
    
    int savedCount;
    if (fscanf(file, "%d\n", &savedCount) != 1 || savedCount <= 0) {
        fclose(file);
        return;
    }
    
    // Allocate memory for saved windows
    WindowInfo* savedWindows = (WindowInfo*)malloc(savedCount * sizeof(WindowInfo));
    if (!savedWindows) {
        fclose(file);
        return;
    }
    
    int loadedCount = 0;
    char line[512];
    
    while (fgets(line, sizeof(line), file) && loadedCount < savedCount) {
        void* hwndPtr;
        wchar_t title[256];
        
        // Parse the line (handle|title)
        char* delimiter = strchr(line, '|');
        if (!delimiter) continue;
        
        *delimiter = '\0';
        sscanf(line, "%p", &hwndPtr);
        
        // Convert the title part to wide char
        MultiByteToWideChar(CP_UTF8, 0, delimiter + 1, -1, title, 256);
        
        // Remove newline from title
        int len = wcslen(title);
        if (len > 0 && title[len-1] == L'\n') {
            title[len-1] = L'\0';
        }
        
        HWND hwnd = (HWND)hwndPtr;
        
        // Only add if window still exists and is valid
        if (IsWindow(hwnd) && IsValidWindow(hwnd)) {
            savedWindows[loadedCount].hwnd = hwnd;
            wcscpy(savedWindows[loadedCount].title, title);
            GetClassNameW(hwnd, savedWindows[loadedCount].className, 256);
            loadedCount++;
        }
    }
    
    fclose(file);
    
    if (loadedCount > 0) {
        g_windows = (WindowInfo*)realloc(savedWindows, loadedCount * sizeof(WindowInfo));
        g_windowCount = loadedCount;
        g_orderInitialized = TRUE;
    } else {
        free(savedWindows);
    }
}