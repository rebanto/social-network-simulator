// Enable Visual Styles (Theming) - THIS MUST BE AT THE VERY TOP
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <windows.h>
#include <commctrl.h> // Required for common controls like ListView
#include <string>
#include <vector>
#include <sstream> // For joining interests
#include <algorithm> // For std::remove

// Assuming these are in the same directory or include paths are set
#include "User.h"
#include "SocialNetwork.h"

// Link with Comctl32.lib
#pragma comment(lib, "Comctl32.lib")

// Helper function to convert std::string to std::wstring
std::wstring s2ws(const std::string& s) {
    int len;
    int slength = (int)s.length() + 1;
    len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
    std::wstring r(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, &r[0], len);
    return r;
}

// Helper function to join vector of strings into a comma-separated wstring
std::wstring join_strings(const std::vector<std::string>& vec, const std::wstring& delimiter = L", ") {
    std::wstringstream wss;
    for (size_t i = 0; i < vec.size(); ++i) {
        wss << s2ws(vec[i]);
        if (i < vec.size() - 1) {
            wss << delimiter;
        }
    }
    return wss.str();
}

// Global variable for the window class name
const char CLASS_NAME[] = "SocialNetworkAppWindowClass";
// Global variable for ListView handle
HWND g_hListView = NULL;
// Global variable for Connections ListView handle
HWND g_hConnectionsListView = NULL;
// Global Font Handle
HFONT g_hFont = NULL;
// Global SocialNetwork instance
SocialNetwork g_socialNetwork;
// Global Add User Popup Window Handle
HWND g_hAddUserPopup = NULL;
// Global Select Second User Popup Window Handle
HWND g_hSelectSecondUserPopup = NULL;
// Global variable to pass the ID of the first user for connection
int g_firstUserIdForConn = -1;


// Control IDs
#define IDC_USERLISTVIEW 101
#define IDC_CONNECTIONSVIEW 102
#define IDC_ADDUSERBUTTON 103
#define IDC_ADDCONNECTIONBUTTON 104

// Add User Popup Controls (200 range)
#define IDC_LABEL_USERNAME 201
#define IDC_EDIT_USERNAME 202
#define IDC_LABEL_NAME 203
#define IDC_EDIT_NAME 204
#define IDC_LABEL_AGE 205
#define IDC_EDIT_AGE 206
#define IDC_LABEL_INTERESTS 207
#define IDC_EDIT_INTERESTS 208
#define IDC_BUTTON_OK_ADDUSER 209
#define IDC_BUTTON_CANCEL_ADDUSER 210

// Select Second User Popup Controls (300 range)
#define IDC_LABEL_SELECTUSER2 301
#define IDC_COMBO_SELECTUSER2 302
#define IDC_BUTTON_OK_ADDCONN 303
#define IDC_BUTTON_CANCEL_ADDCONN 304


const wchar_t ADD_USER_POPUP_CLASS[] = L"AddUserPopupClass";
const wchar_t SELECT_SECOND_USER_POPUP_CLASS[] = L"SelectSecondUserPopupClass";

// Helper function to convert std::wstring to std::string
std::string ws2s(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// Helper function to parse comma-separated interests string into vector<string>
std::vector<std::string> parse_interests(const std::wstring& interests_wstr) {
    std::vector<std::string> interests_vec;
    std::string interests_str = ws2s(interests_wstr);
    std::stringstream ss(interests_str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // Trim leading/trailing whitespace
        item.erase(0, item.find_first_not_of(" \t\n\r\f\v"));
        item.erase(item.find_last_not_of(" \t\n\r\f\v") + 1);
        if (!item.empty()) {
            interests_vec.push_back(item);
        }
    }
    return interests_vec;
}

// Function to initialize and add columns to the ListView
void InitListViewColumns(HWND hWndListView) {
    LVCOLUMN lvc;
    memset(&lvc, 0, sizeof(LVCOLUMN));
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    // Add "ID" column
    lvc.cx = 50;
    lvc.pszText = "ID";
    ListView_InsertColumn(hWndListView, 0, &lvc);

    // Add "Name" column
    lvc.cx = 150;
    lvc.pszText = "Name";
    ListView_InsertColumn(hWndListView, 1, &lvc);

    // Add "Age" column
    lvc.cx = 50;
    lvc.pszText = "Age";
    ListView_InsertColumn(hWndListView, 2, &lvc);

    // Add "Interests" column
    lvc.cx = 200;
    lvc.pszText = "Interests";
    ListView_InsertColumn(hWndListView, 3, &lvc);
}

// Function to initialize and add columns to the Connections ListView
void InitConnectionsListViewColumns(HWND hWndListView) {
    LVCOLUMN lvc;
    memset(&lvc, 0, sizeof(LVCOLUMN));
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    // Add "Connected User ID" column
    lvc.cx = 100;
    lvc.pszText = "Conn. User ID";
    ListView_InsertColumn(hWndListView, 0, &lvc);

    // Add "Name" column
    lvc.cx = 150;
    lvc.pszText = "Name";
    ListView_InsertColumn(hWndListView, 1, &lvc);
}

// Helper function to add an item to the Connections ListView
void AddConnectionItem(HWND hWndListView, int index, int id, const std::string& name) {
    LVITEM lvi;
    memset(&lvi, 0, sizeof(LVITEM));
    lvi.mask = LVIF_TEXT;
    lvi.iItem = index;

    std::wstring w_id = std::to_wstring(id);
    std::wstring w_name = s2ws(name);

    lvi.pszText = &w_id[0];
    ListView_InsertItem(hWndListView, &lvi);
    ListView_SetItemText(hWndListView, index, 1, &w_name[0]);
}

// Function to populate connections based on selected user ID using real network data
void PopulateConnections(HWND hWndConnectionsView, int selectedUserID) {
    ListView_DeleteAllItems(hWndConnectionsView); // Clear previous items

    // Assume SocialNetwork class has a method like getConnections(userId)
    // which returns std::vector<int> of connected user IDs.
    // This method needs to be added to SocialNetwork.h/cpp.
    std::vector<int> connectedIDs;
    User* selectedUser = g_socialNetwork.getUserById(selectedUserID);
    if (selectedUser) { // Only proceed if the selected user is valid
        try {
            // This is the ideal way, assuming getConnections is added to SocialNetwork
            connectedIDs = g_socialNetwork.getConnections(selectedUserID);
        } catch (const std::out_of_range& oor) {
            // Handle cases where selectedUserID might not be in the adjacencyList
            // (e.g., if getConnections throws if user not found or has no entry)
            // For now, just means connectedIDs will be empty, which is fine.
        }
    }

    int itemIndex = 0;
    for (int connectedUserId : connectedIDs) {
        User* connectedUser = g_socialNetwork.getUserById(connectedUserId);
        if (connectedUser) {
            // AddConnectionItem expects std::string for name, which User::getName() should provide
            AddConnectionItem(hWndConnectionsView, itemIndex++, connectedUser->getId(), connectedUser->getName());
        }
    }
}


// Function to populate the main User ListView from SocialNetwork data
void PopulateUserListView(HWND hWndListView, SocialNetwork& network) {
    ListView_DeleteAllItems(hWndListView); // Clear previous items

    std::vector<User> users = network.getAllUsersData(); // Assuming this is the correct method name

    LVITEM lvi;
    memset(&lvi, 0, sizeof(LVITEM));
    lvi.mask = LVIF_TEXT;

    for (int i = 0; i < users.size(); ++i) {
        lvi.iItem = i; // Row index

        // Convert data to wstring for display
        std::wstring w_id = std::to_wstring(users[i].getId());
        std::wstring w_name = s2ws(users[i].getName()); // Assuming User has getName() for display name
        std::wstring w_age = std::to_wstring(users[i].getAge());
        std::wstring w_interests = join_strings(users[i].getInterests());

        lvi.pszText = &w_id[0];
        ListView_InsertItem(hWndListView, &lvi);
        ListView_SetItemText(hWndListView, i, 1, &w_name[0]);
        ListView_SetItemText(hWndListView, i, 2, &w_age[0]);
        ListView_SetItemText(hWndListView, i, 3, &w_interests[0]);
    }
}

// Window procedure to handle messages
// Forward declarations
LRESULT CALLBACK AddUserPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SelectSecondUserPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void ShowAddUserDialog(HWND hParent) {
    if (g_hAddUserPopup) {
        SetFocus(g_hAddUserPopup);
        return;
    }

    WNDCLASSEXW wc = {0};
    HINSTANCE hInstance = GetModuleHandle(NULL);
    if (!GetClassInfoExW(hInstance, ADD_USER_POPUP_CLASS, &wc)) {
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = AddUserPopupWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = ADD_USER_POPUP_CLASS;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);
    }

    int popupWidth = 320;
    int popupHeight = 280;
    RECT parentRect;
    GetWindowRect(hParent, &parentRect);
    int x = parentRect.left + (parentRect.right - parentRect.left - popupWidth) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - popupHeight) / 2;

    g_hAddUserPopup = CreateWindowExW( WS_EX_DLGMODALFRAME, ADD_USER_POPUP_CLASS, L"Add New User",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, popupWidth, popupHeight,
        hParent, NULL, hInstance, NULL );

    if (g_hAddUserPopup) {
        EnableWindow(hParent, FALSE);
        ShowWindow(g_hAddUserPopup, SW_SHOW);
        UpdateWindow(g_hAddUserPopup);
    } else {
        MessageBoxW(hParent, L"Failed to create Add User dialog!", L"Error", MB_OK | MB_ICONERROR);
    }
}

void ShowSelectSecondUserDialog(HWND hParent, int firstUserId) {
    if (g_hSelectSecondUserPopup) {
        SetFocus(g_hSelectSecondUserPopup);
        return;
    }
    g_firstUserIdForConn = firstUserId; // Store the first user's ID

    WNDCLASSEXW wc = {0};
    HINSTANCE hInstance = GetModuleHandle(NULL);
    if (!GetClassInfoExW(hInstance, SELECT_SECOND_USER_POPUP_CLASS, &wc)) {
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = SelectSecondUserPopupWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = SELECT_SECOND_USER_POPUP_CLASS;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);
    }

    int popupWidth = 300;
    int popupHeight = 180; // Adjusted for ComboBox and buttons
    RECT parentRect;
    GetWindowRect(hParent, &parentRect);
    int x = parentRect.left + (parentRect.right - parentRect.left - popupWidth) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - popupHeight) / 2;

    g_hSelectSecondUserPopup = CreateWindowExW( WS_EX_DLGMODALFRAME, SELECT_SECOND_USER_POPUP_CLASS, L"Select User to Connect",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, popupWidth, popupHeight,
        hParent, NULL, hInstance, NULL );

    if (g_hSelectSecondUserPopup) {
        EnableWindow(hParent, FALSE);
        ShowWindow(g_hSelectSecondUserPopup, SW_SHOW);
        UpdateWindow(g_hSelectSecondUserPopup);
    } else {
        MessageBoxW(hParent, L"Failed to create Select Second User dialog!", L"Error", MB_OK | MB_ICONERROR);
        g_firstUserIdForConn = -1; // Reset if dialog creation failed
    }
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
        {
            // Initialize common controls
            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC = ICC_LISTVIEW_CLASSES; // Ensures ListView class is registered.
            InitCommonControlsEx(&icex);

            RECT rcClient;
            GetClientRect(hwnd, &rcClient);

            // Create Font
            g_hFont = CreateFontW(
                -MulDiv(9, GetDeviceCaps(GetDC(hwnd), LOGPIXELSY), 72), // Height 9pt
                0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI"); // Font name

            if (g_hFont == NULL) { // Fallback font if Segoe UI is not available
                g_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            }

            int listViewWidth = (rcClient.right - rcClient.left) / 2;
            int connectionsListViewWidth = (rcClient.right - rcClient.left) - listViewWidth; // Ensure it fills
            int controlHeight = 25;
            int buttonWidth = 120; // Width for "Add User"
            int buttonWidth2 = 140; // Width for "Add Connection"
            int currentY = 10;
            int padding = 5;

            // Add User Button
            HWND hAddUserButton = CreateWindowExW(0, L"BUTTON", L"Add User",
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                10, currentY, buttonWidth, controlHeight,
                hwnd, (HMENU)IDC_ADDUSERBUTTON, GetModuleHandle(NULL), NULL);
            SendMessage(hAddUserButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            // Add Connection Button
            HWND hAddConnButton = CreateWindowExW(0, L"BUTTON", L"Add Connection",
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                10 + buttonWidth + padding, currentY, buttonWidth2, controlHeight,
                hwnd, (HMENU)IDC_ADDCONNECTIONBUTTON, GetModuleHandle(NULL), NULL);
            SendMessage(hAddConnButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            currentY += controlHeight + padding;

            int listViewHeight = rcClient.bottom - currentY;


            // Create the main User ListView (left half)
            g_hListView = CreateWindowEx(
                0, WC_LISTVIEW, "",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_EDITLABELS,
                0, currentY, listViewWidth, listViewHeight, // x, y, width, height
                hwnd, (HMENU)IDC_USERLISTVIEW, GetModuleHandle(NULL), NULL);

            if (g_hListView == NULL) {
                MessageBox(hwnd, "Could not create user list view.", "Error", MB_OK | MB_ICONERROR);
                return -1;
            }
            InitListViewColumns(g_hListView);
            SendMessage(g_hListView, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            PopulateUserListView(g_hListView, g_socialNetwork); // Populate with real data

            // Create the Connections ListView (right half)
            g_hConnectionsListView = CreateWindowEx(
                0, WC_LISTVIEW, "",
                WS_CHILD | WS_VISIBLE | LVS_REPORT,
                listViewWidth, currentY, connectionsListViewWidth, listViewHeight, // x, y, width, height
                hwnd, (HMENU)IDC_CONNECTIONSVIEW, GetModuleHandle(NULL), NULL);

            if (g_hConnectionsListView == NULL) {
                MessageBox(hwnd, "Could not create connections list view.", "Error", MB_OK | MB_ICONERROR);
                return -1;
            }
            InitConnectionsListViewColumns(g_hConnectionsListView);
            SendMessage(g_hConnectionsListView, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        }
        break;
        case WM_SIZE:
        {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);

            HWND hAddUserBtn = GetDlgItem(hwnd, IDC_ADDUSERBUTTON);
            HWND hAddConnBtn = GetDlgItem(hwnd, IDC_ADDCONNECTIONBUTTON);

            RECT addUserBtnRect = {0}; // For width/height calculations
            if (hAddUserBtn) GetClientRect(hAddUserBtn, &addUserBtnRect);
            int addUserBtnWidth = addUserBtnRect.right - addUserBtnRect.left;
            int btnHeight = addUserBtnRect.bottom - addUserBtnRect.top;

            // Fallbacks if controls not fully initialized or visible yet
            if (addUserBtnWidth <=0) addUserBtnWidth = 120;
            if (btnHeight <=0) btnHeight = 25;

            RECT addConnBtnRect = {0};
            if (hAddConnBtn) GetClientRect(hAddConnBtn, &addConnBtnRect);
            int addConnBtnWidth = addConnBtnRect.right - addConnBtnRect.left;
            if (addConnBtnWidth <=0) addConnBtnWidth = 140;


            int currentY = 10;
            int padding = 5;

            if (hAddUserBtn) MoveWindow(hAddUserBtn, 10, currentY, addUserBtnWidth, btnHeight, TRUE);
            if (hAddConnBtn) MoveWindow(hAddConnBtn, 10 + addUserBtnWidth + padding, currentY, addConnBtnWidth, btnHeight, TRUE);

            currentY += btnHeight + padding; // Y position for ListViews

            int listHeight = height - currentY;
            if (listHeight < 0) listHeight = 0;

            int halfWidth = width / 2;
            int secondHalfWidth = width - halfWidth;

            if (g_hListView && g_hConnectionsListView) {
                MoveWindow(g_hListView, 0, currentY, halfWidth, listHeight, TRUE);
                MoveWindow(g_hConnectionsListView, halfWidth, currentY, secondHalfWidth, listHeight, TRUE);
            }
        }
        break;
        case WM_COMMAND:
            if (HIWORD(wParam) == BN_CLICKED) {
                switch (LOWORD(wParam)) {
                    case IDC_ADDUSERBUTTON:
                        ShowAddUserDialog(hwnd);
                        break;
                    case IDC_ADDCONNECTIONBUTTON: {
                        int selectedItemIndex = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
                        if (selectedItemIndex == -1) {
                            MessageBoxW(hwnd, L"Please select the first user from the list.", L"Select User 1", MB_OK | MB_ICONINFORMATION);
                            return 0;
                        }
                        wchar_t userIdStr[32];
                        ListView_GetItemText(g_hListView, selectedItemIndex, 0, userIdStr, 32);
                        int firstUserId = _wtoi(userIdStr);
                        ShowSelectSecondUserDialog(hwnd, firstUserId);
                        break;
                    }
                }
            }
            break;
        case WM_NOTIFY:
        {
            LPNMHDR lpnmh = (LPNMHDR)lParam;
            if (lpnmh->hwndFrom == g_hListView && lpnmh->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW pNMLV = (LPNMLISTVIEW)lParam;
                if (pNMLV->iItem != -1 && (pNMLV->uNewState & LVIS_SELECTED) && !(pNMLV->uOldState & LVIS_SELECTED)) {
                    // Item selected
                    wchar_t userID_wstr[32];
                    ListView_GetItemText(g_hListView, pNMLV->iItem, 0, userID_wstr, sizeof(userID_wstr)/sizeof(wchar_t));
                    int selectedUserID = _wtoi(userID_wstr); // Convert wchar_t* to int

                    PopulateConnections(g_hConnectionsListView, selectedUserID);

                    // Optional: Display MessageBox for user details (can be removed if connections view is enough)
                    // To retrieve full user details for a MessageBox, you'd fetch from g_socialNetwork using selectedUserID
                    // User* selectedUser = g_socialNetwork.getUserById(selectedUserID);
                    // if (selectedUser) {
                    //    std::wstring name = s2ws(selectedUser->getName());
                    //    std::wstring age = std::to_wstring(selectedUser->getAge());
                    //    std::wstring interests_str = join_strings(selectedUser->getInterests());
                    //    wchar_t buffer[1024];
                    //    wsprintfW(buffer, L"User Profile:\n\nID: %d\nName: %s\nAge: %s\nInterests: %s",
                    //              selectedUserID, name.c_str(), age.c_str(), interests_str.c_str());
                    //    MessageBoxW(hwnd, buffer, L"User Details", MB_OK | MB_ICONINFORMATION);
                    // }
                } else if (!(pNMLV->uNewState & LVIS_SELECTED) && (pNMLV->uOldState & LVIS_SELECTED)) {
                    // Item deselected
                    ListView_DeleteAllItems(g_hConnectionsListView);
                }
            }
        }
        break;
        case WM_DESTROY:
            if (g_hFont) {
                DeleteObject(g_hFont);
            }
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Application entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize Social Network with some data
    // User IDs are auto-incremented starting from 1 by SocialNetwork class
    g_socialNetwork.addUser("alice_w", "Alice Wonderland", 30, {"Reading", "Tea Parties"});
    g_socialNetwork.addUser("bob_b", "Bob The Builder", 45, {"Building", "Fixing"});
    g_socialNetwork.addUser("charlie_b", "Charlie Brown", 8, {"Baseball", "Flying Kites"});
    g_socialNetwork.addUser("diana_p", "Diana Prince", 3000, {"Justice", "Truth"});

    g_socialNetwork.addConnection(1, 2); // Alice (1) - Bob (2)
    g_socialNetwork.addConnection(1, 3); // Alice (1) - Charlie (3)
    g_socialNetwork.addConnection(2,1);  // Bob (2) - Alice (1) (symmetric)


    // Register the window class
    WNDCLASSEXW wc = {0}; // Use W version for wide strings
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME; // Still char for main window class name as defined
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassExA(&wc)) { // Use RegisterClassExA if CLASS_NAME is char
        MessageBoxA(NULL, "Window Registration Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Create the main application window
    HWND hwnd = CreateWindowExA( // Use A version if CLASS_NAME is char
        0,                              // Optional window styles.
        CLASS_NAME,                     // Window class
        "Social Network App",           // Window text
        WS_OVERLAPPEDWINDOW,            // Window style

        // Size and position
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, // Initial size

        NULL,       // Parent window
        NULL,       // Menu
        hInstance,  // Instance handle
        NULL        // Additional application data
    );

    if (hwnd == NULL) {
        MessageBoxA(NULL, "Window Creation Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message loop
    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        // Handle messages for both popups if they exist
        HWND activePopup = NULL;
        if (g_hAddUserPopup) activePopup = g_hAddUserPopup;
        else if (g_hSelectSecondUserPopup) activePopup = g_hSelectSecondUserPopup;

        if (activePopup == NULL || !IsDialogMessage(activePopup, &msg)) {
             TranslateMessage(&msg);
             DispatchMessage(&msg);
        }
    }

    return msg.wParam;
}

// Window Procedure for the Add User Popup
LRESULT CALLBACK AddUserPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hEditUsername, hEditName, hEditAge, hEditInterests; // Made static for WM_COMMAND access

    switch (msg) {
        case WM_CREATE: {
            int currentY = 10;
            int labelWidth = 80;
            int editWidth = 200;
            int controlHeight = 20;
            int buttonHeight = 25;
            int padding = 5;
            HINSTANCE hInstance = GetModuleHandle(NULL);

            CreateWindowExW(0, L"STATIC", L"Username:", WS_CHILD | WS_VISIBLE, 10, currentY, labelWidth, controlHeight, hwnd, (HMENU)IDC_LABEL_USERNAME, hInstance, NULL);
            hEditUsername = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10 + labelWidth + padding, currentY, editWidth, controlHeight, hwnd, (HMENU)IDC_EDIT_USERNAME, hInstance, NULL);
            currentY += controlHeight + padding;
            CreateWindowExW(0, L"STATIC", L"Full Name:", WS_CHILD | WS_VISIBLE, 10, currentY, labelWidth, controlHeight, hwnd, (HMENU)IDC_LABEL_NAME, hInstance, NULL);
            hEditName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10 + labelWidth + padding, currentY, editWidth, controlHeight, hwnd, (HMENU)IDC_EDIT_NAME, hInstance, NULL);
            currentY += controlHeight + padding;
            CreateWindowExW(0, L"STATIC", L"Age:", WS_CHILD | WS_VISIBLE, 10, currentY, labelWidth, controlHeight, hwnd, (HMENU)IDC_LABEL_AGE, hInstance, NULL);
            hEditAge = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 10 + labelWidth + padding, currentY, editWidth, controlHeight, hwnd, (HMENU)IDC_EDIT_AGE, hInstance, NULL);
            currentY += controlHeight + padding;
            CreateWindowExW(0, L"STATIC", L"Interests:", WS_CHILD | WS_VISIBLE, 10, currentY, labelWidth, controlHeight, hwnd, (HMENU)IDC_LABEL_INTERESTS, hInstance, NULL);
            hEditInterests = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10 + labelWidth + padding, currentY, editWidth, controlHeight, hwnd, (HMENU)IDC_EDIT_INTERESTS, hInstance, NULL);
            currentY += controlHeight + padding;
            CreateWindowExW(0, L"STATIC", L"(comma-separated)", WS_CHILD | WS_VISIBLE, 10 + labelWidth + padding, currentY, editWidth, controlHeight, hwnd, NULL, hInstance, NULL); // Info label
            currentY += controlHeight + padding;
            CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 60, currentY, 80, buttonHeight, hwnd, (HMENU)IDC_BUTTON_OK_ADDUSER, hInstance, NULL);
            CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 160, currentY, 80, buttonHeight, hwnd, (HMENU)IDC_BUTTON_CANCEL_ADDUSER, hInstance, NULL);

            // Apply font (assuming g_hFont is valid)
            SendMessage(GetDlgItem(hwnd, IDC_LABEL_USERNAME), WM_SETFONT, (WPARAM)g_hFont, TRUE); SendMessage(hEditUsername, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(GetDlgItem(hwnd, IDC_LABEL_NAME), WM_SETFONT, (WPARAM)g_hFont, TRUE); SendMessage(hEditName, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(GetDlgItem(hwnd, IDC_LABEL_AGE), WM_SETFONT, (WPARAM)g_hFont, TRUE); SendMessage(hEditAge, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(GetDlgItem(hwnd, IDC_LABEL_INTERESTS), WM_SETFONT, (WPARAM)g_hFont, TRUE); SendMessage(hEditInterests, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(GetDlgItem(hwnd, IDC_BUTTON_OK_ADDUSER), WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(GetDlgItem(hwnd, IDC_BUTTON_CANCEL_ADDUSER), WM_SETFONT, (WPARAM)g_hFont, TRUE);
            // For the info label without an ID, you might need to iterate child windows or give it an ID too.
            // For now, this is okay.

            SetFocus(hEditUsername);
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_BUTTON_OK_ADDUSER: {
                    wchar_t wUsername[100], wName[100], wAge[10], wInterests[256];
                    GetWindowTextW(hEditUsername, wUsername, 100); GetWindowTextW(hEditName, wName, 100);
                    GetWindowTextW(hEditAge, wAge, 10); GetWindowTextW(hEditInterests, wInterests, 256);
                    std::string username = ws2s(wUsername); std::string name = ws2s(wName); std::string age_str = ws2s(wAge);
                    if (username.empty() || name.empty() || age_str.empty()) { MessageBoxW(hwnd, L"Username, Name, and Age cannot be empty.", L"Validation Error", MB_OK | MB_ICONWARNING); return 0; }
                    int age = 0; try { age = std::stoi(age_str); if (age <= 0) throw std::invalid_argument("Age must be positive"); } catch (const std::exception&) { MessageBoxW(hwnd, L"Invalid Age. Please enter a positive number.", L"Validation Error", MB_OK | MB_ICONWARNING); SetFocus(hEditAge); return 0; }
                    std::vector<std::string> interests = parse_interests(wInterests);
                    g_socialNetwork.addUser(username, name, age, interests);
                    PopulateUserListView(g_hListView, g_socialNetwork); // Refresh main list
                    EnableWindow(GetParent(hwnd), TRUE); DestroyWindow(hwnd); return 0;
                }
                case IDC_BUTTON_CANCEL_ADDUSER: { EnableWindow(GetParent(hwnd), TRUE); DestroyWindow(hwnd); return 0; }
            }
            break;
        }

        case WM_CLOSE: EnableWindow(GetParent(hwnd), TRUE); DestroyWindow(hwnd); return 0;
        case WM_DESTROY: g_hAddUserPopup = NULL; return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


// Window Procedure for the Select Second User Popup
LRESULT CALLBACK SelectSecondUserPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hComboSelectUser2; // Static to be accessible in WM_COMMAND

    switch (msg) {
        case WM_CREATE: {
            int currentY = 10;
            int labelWidth = 100;
            int comboWidth = 180;
            int controlHeight = 20; // Height for label
            int comboHeight = 25;   // Height for combobox itself
            int buttonHeight = 25;
            int padding = 5;
            HINSTANCE hInstance = GetModuleHandle(NULL);

            CreateWindowExW(0, L"STATIC", L"Select User 2:", WS_CHILD | WS_VISIBLE, 10, currentY + padding, labelWidth, controlHeight, hwnd, (HMENU)IDC_LABEL_SELECTUSER2, hInstance, NULL);
            // currentY += controlHeight + padding; // Adjust Y for ComboBox

            hComboSelectUser2 = CreateWindowExW(0, WC_COMBOBOXW, L"",
                                              CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE | WS_VSCROLL,
                                              10 + labelWidth + padding, currentY, comboWidth, comboHeight + 100, // Height includes dropdown part
                                              hwnd, (HMENU)IDC_COMBO_SELECTUSER2, hInstance, NULL);
            currentY += comboHeight + padding + 10; // Extra padding after combo, and account for its visible part

            // Populate ComboBox
            std::vector<User> users = g_socialNetwork.getAllUsersData();
            for (const auto& user : users) {
                if (user.getId() == g_firstUserIdForConn) continue; // Skip the first user

                std::wstring userNameDisplay = L"ID: " + std::to_wstring(user.getId()) + L", " + s2ws(user.getName());
                LRESULT index = SendMessageW(hComboSelectUser2, CB_ADDSTRING, 0, (LPARAM)userNameDisplay.c_str());
                if (index != CB_ERR && index != CB_ERRSPACE) {
                    SendMessageW(hComboSelectUser2, CB_SETITEMDATA, (WPARAM)index, (LPARAM)user.getId());
                }
            }
            if (SendMessage(hComboSelectUser2, CB_GETCOUNT, 0,0) > 0) {
                 SendMessage(hComboSelectUser2, CB_SETCURSEL, 0, 0); // Select first item
            }

            currentY += 20; // More space before buttons


            HWND hButtonOK = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 40, currentY, 80, buttonHeight, hwnd, (HMENU)IDC_BUTTON_OK_ADDCONN, hInstance, NULL);
            HWND hButtonCancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 140, currentY, 80, buttonHeight, hwnd, (HMENU)IDC_BUTTON_CANCEL_ADDCONN, hInstance, NULL);

            // Apply font
            SendMessage(GetDlgItem(hwnd, IDC_LABEL_SELECTUSER2), WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(hComboSelectUser2, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(hButtonOK, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(hButtonCancel, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            SetFocus(hComboSelectUser2);
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_BUTTON_OK_ADDCONN: {
                    LRESULT selIndex = SendMessageW(hComboSelectUser2, CB_GETCURSEL, 0, 0);
                    if (selIndex == CB_ERR) {
                        MessageBoxW(hwnd, L"Please select a user from the list.", L"No User Selected", MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    int secondUserId = (int)SendMessageW(hComboSelectUser2, CB_GETITEMDATA, (WPARAM)selIndex, 0);

                    bool success = g_socialNetwork.addConnection(g_firstUserIdForConn, secondUserId);
                    if (success) { // Also add symmetric connection if the network doesn't do it.
                         g_socialNetwork.addConnection(secondUserId, g_firstUserIdForConn);
                    } else {
                        // Optional: Notify user if connection already exists or failed for other reasons
                        // MessageBoxW(hwnd, L"Failed to create connection. Users might already be connected.", L"Connection Info", MB_OK | MB_ICONINFORMATION);
                    }

                    // Refresh connections list if the currently selected user in main list is firstUserId
                    int currentMainListSelectionIdx = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
                    if (currentMainListSelectionIdx != -1) {
                        wchar_t mainListUserIdStr[32];
                        ListView_GetItemText(g_hListView, currentMainListSelectionIdx, 0, mainListUserIdStr, 32);
                        if (_wtoi(mainListUserIdStr) == g_firstUserIdForConn) {
                            PopulateConnections(g_hConnectionsListView, g_firstUserIdForConn);
                        }
                    }

                    EnableWindow(GetParent(hwnd), TRUE);
                    DestroyWindow(hwnd);
                    return 0;
                }
                case IDC_BUTTON_CANCEL_ADDCONN: {
                    EnableWindow(GetParent(hwnd), TRUE);
                    DestroyWindow(hwnd);
                    return 0;
                }
            }
            break;
        }
        case WM_CLOSE:
            EnableWindow(GetParent(hwnd), TRUE);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_hSelectSecondUserPopup = NULL;
            g_firstUserIdForConn = -1; // Reset
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
