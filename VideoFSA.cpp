// VideoFSA.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "VideoFSA.h"
#include "stdio.h""

#define MAX_LOADSTRING 100


//===========================================================================
//	VideoFSA code
//	(C) 2024 Matt Regan.
//	Free for use for any purpose as defined under the original GNU license agreement.
//
//	The VideoSFA is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
//	GNU General Public License for more details.
//


#define HORIZONTALTOTAL     57
#define HORIZONTALACTIVE    32
#define HSYNCSTART          41         
#define HSYNCWIDTH          7

#define VERTICALTOTAL       525
#define VERTICALACTIVE      384
#define VSYNCSTART          454
#define VSYNCWIDTH          2

#define EVENADDRESS         0x0000
#define ODDADDRESS          0x2000
#define HORIZONTALBORDER    0x4000
#define VERTICALBORDER      0x8000
#define SYNCADDRESS         0xC000
#define INTERRUPTWIDTH      15

#define HSYNC               0x1000
#define VSYNC               0x2000
#define EPROMSIZE           0x10000

bool finished = false;

//	Data comes int assuming data signals are contiguous on the 27c322 ROM
//	Data goes out in the correct position on the chip
unsigned int SwizzleData(unsigned int val)
{
    unsigned int ret;

    ret = 0;
    for (int i = 0; i < 16; i++) {
        if (val & (1 << i)) ret |= ((i & 1) ? (1 << ((i & 14) >> 1) + 8) : (1 << ((i >> 1))));
    }
    for (int i = 16; i < 32; i++) {
        if (val & (1 << i)) ret |= ((i & 1) ? (1 << ((i & 14) >> 1) + 24) : (1 << (((i & 14) >> 1)) + 16));
    }
    return ret;
}

//	Data in the correct position on the chip
//	Data goes out assuming data signals are contiguous on the 27c322 ROM
//  The native pinout of the chip effectively performs the Deswizzle from pinout to circuit
unsigned int DeSwizzleData(unsigned int val)
{
    unsigned int ret;

    ret = 0;
    for (int i = 0; i < 16; i++) {
        if (val & (1 << i)) ret |= ((i & 8) ? (1 << (((i & 7) * 2) + 1)) : (1 << (i * 2)));
    }
    for (int i = 16; i < 32; i++) {
        if (val & (1 << i)) ret |= ((i & 8) ? (1 << (((i & 7) * 2) + 17)) : (1 << (((i & 7) * 2) + 16)));
    }
    return ret;
}


void    SwapSpectrumBits(unsigned int* address)
{
    //   return;
    unsigned int value = *address;
    *address = (value & 0xf81f) | ((value >> 3) & 0x00e0) | ((value << 3) & 0x0700);
}

//  Complex mapping for raster generator.
unsigned int    ComputeAddress(int baserow, int basecolumn)
{
    int row = baserow;
    int column = basecolumn;
    unsigned int address = 0x0000;
    if (column >= HORIZONTALTOTAL) {
        column = 0;
        row++;
        if (row >= VERTICALTOTAL) row = 0;
    }
    if ((row < VERTICALACTIVE) && (column < HORIZONTALACTIVE)) {
        address = ((row/2) * HORIZONTALACTIVE + column) + ((row & 0x01) ? ODDADDRESS : EVENADDRESS);
        SwapSpectrumBits(&address);
        return address;
    }

    if ((row == VSYNCSTART - 1) && (column < INTERRUPTWIDTH)) {
        address = 0xc800 + column;
        SwapSpectrumBits(&address);
        return address;
    }
    if (((column >= HSYNCSTART) && (column < (HSYNCSTART + HSYNCWIDTH))) ||
        ((row >= VSYNCSTART) && (row < (VSYNCSTART + VSYNCWIDTH)))) {           //  We are in sync
        address = SYNCADDRESS;
        if ((column >= HSYNCSTART) && (column < (HSYNCSTART + HSYNCWIDTH))) {   //  HSYNC is active
            if ((row >= VSYNCSTART) && (row < (VSYNCSTART + VSYNCWIDTH))) address |= VSYNC;
            address |= (HSYNC + (row * HSYNCWIDTH) + (column - HSYNCSTART));
            SwapSpectrumBits(&address);
            return address;
        }
        else {
            address |= (VSYNC + (row - VSYNCSTART)*HORIZONTALTOTAL + (column));
            SwapSpectrumBits(&address);
            return address;
        }
    }
  
    if (column == (HSYNCSTART - 1)) {
        address = SYNCADDRESS + row;
        SwapSpectrumBits(&address);
        return address;
    }
    if (column == (HSYNCSTART + HSYNCWIDTH)) {
        address = SYNCADDRESS + row + 0x400;
        SwapSpectrumBits(&address);
        return address;
    }

    if (row < VERTICALACTIVE) {
        //  Compute horizontal border
        address = HORIZONTALBORDER + (row * (HORIZONTALTOTAL - HORIZONTALACTIVE)) + (column - HORIZONTALACTIVE);
    }
    else {
        //  Compute vertical border
        address = VERTICALBORDER;
        address += ((row - VERTICALACTIVE) * HORIZONTALTOTAL) + column;
    }
    SwapSpectrumBits(&address);
    return address;
}

void    DecodeAddress(int* row, int* column, unsigned int value)
{
    *row = 0;
    *column = 0;
    unsigned int address = value;
    SwapSpectrumBits(&address);

    switch (address & 0xc000) {

    case EVENADDRESS:    
        if (!(address & ODDADDRESS)) {
            *column = address & 0x1f;  *row = ((address >> 5) & 0xff) * 2;
        }
        else {
            *column = address & 0x1f;  *row = ((address >> 5) & 0xff) * 2 + 1;
        }
        break;
    case HORIZONTALBORDER:
        *column = ((address & 0x3fff) % (HORIZONTALTOTAL - HORIZONTALACTIVE)) + HORIZONTALACTIVE;
        *row = (address & 0x3fff) / (HORIZONTALTOTAL - HORIZONTALACTIVE);
        break;
    case VERTICALBORDER:
        *column = (address & 0x3fff) % (HORIZONTALTOTAL);
        *row = ((address & 0x3fff) / (HORIZONTALTOTAL))+VERTICALACTIVE;
        break;
    case SYNCADDRESS:
        if ((address - SYNCADDRESS) < 0x400) {
            *column = HSYNCSTART - 1;
            *row = address - SYNCADDRESS;
        }
        if ((address >= 0xc400)&&(address < 0xc800)) {
            *column = HSYNCSTART + HSYNCWIDTH;
            *row = address - SYNCADDRESS - 0x400;
        }
        if ((address >= 0xc800) && (address < 0xd000)) {
            *column = address - 0xc800;
            *row = VSYNCSTART - 1;
        }

        if (address & HSYNC) {
            *column = ((address & 0x0fff) % HSYNCWIDTH) + HSYNCSTART;
            *row = (address & 0x0fff) / HSYNCWIDTH;
        }
        else {
            if (address & VSYNC) {
                *column = ((address & 0x0fff) % HORIZONTALTOTAL);
                *row = ((address & 0x0fff) / HORIZONTALTOTAL) + VSYNCSTART;
            }

        }
        break;
    }
    return;
}
unsigned int EPROM[EPROMSIZE];

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
HDC g_hdc, g_bufferHdc;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    for (int i = 0; i < EPROMSIZE; i++) {
        EPROM[i] = 0;
    }

    for (int row = 0; row < VERTICALTOTAL; row++) {
        for (int column = 0; column < HORIZONTALTOTAL; column++) {
            unsigned int address = ComputeAddress(row, column);
            unsigned int nextAddress = ComputeAddress(row, column+1);

            EPROM[address] = SwizzleData(nextAddress);
        }
    }

    FILE* fp;
    fopen_s (&fp, "VideoFSA.bin", "w+b");
    for (int i = 0; i < EPROMSIZE; i++) {
        fputc(EPROM[i] & 0xff, fp);
        fputc((EPROM[i]>>8) & 0xff, fp);
    }
    fclose(fp);


    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_VIDEOFSA, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_VIDEOFSA));

    MSG msg;
    unsigned int address = 0;
    unsigned int colour = 0;
    int row, column;

    // Main message loop:
    while (!finished) {
        for (int i = 0; i < 10; i++) {
            DecodeAddress(&row, &column, address);
            if (column == 0) {
                for (int pos = 0; pos < 16 * 57; pos++) SetPixel(g_hdc, column * 16 + pos, row, 0x00ffffff);
            }
            switch (address & 0xc000) {

            case EVENADDRESS:       colour = 0x000000ff;    break;
            case HORIZONTALBORDER:  colour = 0x0000ff00;    break;
            case VERTICALBORDER:    colour = 0x00ff0000;    break;
            case SYNCADDRESS:       
                if (address < 0xc800) {
                    colour = 0x0000ffff;
                }
                else if (address < 0xd000) {
                    colour = 0x00ffff00;
                }
                else {
                    colour = 0x00000000;
                }
                break;
            default:                colour = 0x00ffffff;    break;
            }
            for (int pos = 0; pos < 16; pos++) SetPixel(g_hdc, column * 16 + pos, row, colour);
            address = DeSwizzleData(EPROM[address]);
        }

        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {

            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_VIDEOFSA));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_VIDEOFSA);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   g_hdc = GetDC(hWnd);	// Get device context from window
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                finished = true;
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
