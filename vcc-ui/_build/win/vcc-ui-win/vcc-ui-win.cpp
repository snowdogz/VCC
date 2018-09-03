////////////////////////////////////////////////////////////////////////////////////
//
// vcc-ui-win.cpp : Defines the entry point for the application.
//
// Simple VCC UI implementation (single instance)
//
////////////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "vcc-ui-win.h"

#include "vcc.h"

#include <SDL.h>

////////////////////////////////////////////////////////////////////////////////////

#define MAX_LOADSTRING 100

////////////////////////////////////////////////////////////////////////////////////

typedef struct windoinfo_t
{
	HINSTANCE hInstance;
	HWND hWnd;
	SDL_mutex * mutex;
} windowinfo_t;

////////////////////////////////////////////////////////////////////////////////////
//
// Global Variables:
//

vccinstance_t *	g_vccInstance = NULL;					// instance of the coco virtual machine

														/**
														*/
void displayLockCB(void * pParam)
{
	windowinfo_t * windowinfo = (windowinfo_t *)pParam;

	SDL_LockMutex(windowinfo->mutex);
}

/**
*/
void displayUnlockCB(void * pParam)
{
	windowinfo_t * windowinfo = (windowinfo_t *)pParam;

	SDL_UnlockMutex(windowinfo->mutex);
}

/**
The display flip callback is called when an emulator frame is done
being rendered.  This is not called on a 'skipped' frame.

this function makes the view dirty so the latest emulated frame will
be displayed.  The update is dispatched by the Cocoa framework when
it thinks it is time.
*/
void displayFlipCB(void * pParam)
{
	windowinfo_t * windowinfo = (windowinfo_t *)pParam;

	// hold onto this for the dispatch
	CFBridgingRetain(viewController);

	/// get a snapshot of this frame from the emulator
	//[[viewController emulatorView] captureScreen:vccInstance];

	//NSLog(@"displayFlipCB");

	// trigger redraw of the view on the main thread
	dispatch_async(dispatch_get_main_queue(), ^{
		//dispatch_sync(dispatch_get_main_queue(), ^{

		vccinstance_t * vccInstance = viewController.vccInstance;
	if (vccInstance != NULL)
	{
		VCCVMView * view = [viewController emulatorView];
		NSRect rctViewBounds = [view bounds];
		[view setNeedsDisplayInRect : rctViewBounds];

		// send one mouse event per frame to the instance
		{
			mouseevent_t mouseEvent;

			INIT_MOUSEEVENT(&mouseEvent,eEventMouseMove);

			mouseEvent.width = rctViewBounds.size.width;
			mouseEvent.height = rctViewBounds.size.height;
			mouseEvent.x = viewController.ptMouse.x;
			mouseEvent.y = viewController.ptMouse.y;

			emuRootDevDispatchEvent(&vccInstance->root, &mouseEvent.event);
		}

		// update status text
		[viewController.statusText setStringValue : [NSString stringWithCString : vccInstance->cStatusText encoding : NSASCIIStringEncoding]];

		// TODO: should not be done in the main thread dispatch - move out or have key queue in vcc-core
		// paste text handling
		[viewController handlePasteNextCharacter];
	}

	// release now that main queue dispatch is done
	CFBridgingRelease((__bridge CFTypeRef _Nullable)(viewController));
		});
}

/**
Full screen toggle callback
*/
void displayFullScreenCB(void * pParam)
{
	VCCVMViewController * viewController = (__bridge VCCVMViewController *)pParam;
	VCCVMDocument * document = [viewController document];
	//vccinstance_t * vccInstance = viewController.vccInstance;

	// go through and find the VCCVMWindowController and tell it to toggle full screen
	NSArray< NSWindowController *> * windowControllers = [document windowControllers];
	for (int i = 0; i<windowControllers.count; i++)
	{
		NSWindowController * controller = windowControllers[i];

		if ([controller isKindOfClass : [VCCVMWindowController class]])
		{
			[[controller window] toggleFullScreen:viewController];
		}
	}
}

/**
Update user interface callback

Called from VCC instance

update menu items, toolbar items, etc for this instance
This should only be called when devices, etc change
*/
void updateInterfaceCB(vccinstance_t * pInstance)
{
	if (pInstance != NULL)
	{
		hmenu_t     hMenu;
		NSMenu *    pMenu;

		// grab current menu pointer for destruction
		hMenu = pInstance->root.device.hMenu;

		// remove current instance menu from the main menu
		unhookVMInstanceMenu(pInstance);

		/*
		re-create the entire VM menu for this instance
		*/
		emuDevEnumerate(&pInstance->root.device, updateUIEnumCB, NULL /* pDoc */);

		/*
		iterate items in new menu
		and set action/selector to
		the pApp delegate menuAction method
		*/
		pMenu = (__bridge NSMenu *)pInstance->root.device.hMenu;
		assert(pMenu != NULL);
		setMenuItemsAction(pMenu);

		// add this instance menu back into the main menu
		hookVMInstanceMenu(pInstance);

		/* destroy previous VM instance menu */
		if (hMenu != nil)
		{
			menuDestroy(hMenu);
		}
	}
}

/**
*/
void screenShotCB(vccinstance_t * pInstance)
{
	NSBitmapImageRep * screenCopy;
	screen_t * pScreen = pInstance->pCoco3->pScreen;

	//
	// Capture
	//
	screenCopy = [[NSBitmapImageRep alloc]
		initWithBitmapDataPlanes:(unsigned char **)&pScreen->surface.pBuffer
		pixelsWide : pScreen->surface.width
		pixelsHigh : pScreen->surface.height
		bitsPerSample : 8
		samplesPerPixel : 4
		hasAlpha : YES
		isPlanar : NO
		colorSpaceName : @"NSDeviceRGBColorSpace" //@"NSCalibratedRGBColorSpace" //
		bitmapFormat : 0 //NSAlphaFirstBitmapFormat
		bytesPerRow : pScreen->surface.linePitch
		bitsPerPixel : 32
	];

	//
	// Crop it
	//
	//int videoWidth  = pScreen->PixelsperLine;
	//int videoHeight = pScreen->LinesperScreen;    
	// set up texture coordinates based on amount of the texture used for the current video mode
	//float h = (float)videoWidth;
	//float v = (float)videoHeight;
	float h = (float)pScreen->surface.width;
	float v = (float)pScreen->surface.height;

	CGRect rect = CGRectMake(0, 0, h, v);
	CGImageRef cgImg = CGImageCreateWithImageInRect([screenCopy CGImage],
		rect);
	NSBitmapImageRep *result = [[NSBitmapImageRep alloc]
		initWithCGImage:cgImg];
	CGImageRelease(cgImg);

	//
	// save it
	//
	char * path = sysGetSavePathnameFromUser(NULL, NULL);
	if (path != NULL)
	{
		NSData * image = [result TIFFRepresentation];
		NSString * nstrPath = [NSString stringWithUTF8String : path];
		[image writeToFile : nstrPath atomically : YES];
	}
}

////////////////////////////////////////////////////////////////////////////////////
//
// Windows stuff
//
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
HWND                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

windowinfo_t g_windowInfo;

////////////////////////////////////////////////////////////////////////////////////

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_VCCUIWIN, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
	HWND hWnd = InitInstance(hInstance, nCmdShow);
    if (hWnd == NULL)
    {
        return FALSE;
    }

	g_windowInfo.hWnd = hWnd;
	g_windowInfo.hInstance = hInstance;
	g_windowInfo.mutex = SDL_CreateMutex(); //("display");

	//
	// Initialize VCC instance
	//
	// TODO: load INI file
	g_vccInstance = vccCreate("vcc");
	if (g_vccInstance != NULL)
	{
		/*
		Set up callbacks for giving info to sub modules
		*/
		g_vccInstance->root.pRootObject = &g_windowInfo;

		g_vccInstance->root.pfnGetDocumentPath = vccGetDocumentPath;
		g_vccInstance->root.pfnGetAppPath = vccGetAppPath;
		g_vccInstance->root.pfnLog = vccLog;
		
		//
		// initialize display update callbacks
		//
		// SDL won't let us name the mutex
		//mutex = SDL_CreateMutex(); //("display");
		g_vccInstance->pCoco3->pScreen->pScreenCallbackParam = &g_windowInfo;
		g_vccInstance->pCoco3->pScreen->pScreenFlipCallback = displayFlipCB;
		g_vccInstance->pCoco3->pScreen->pScreenLockCallback = displayLockCB;
		g_vccInstance->pCoco3->pScreen->pScreenUnlockCallback = displayUnlockCB;
		g_vccInstance->pCoco3->pScreen->pScreenFullScreenCallback = displayFullScreenCB;

		//
		// set emulator callback for updating the UI
		// because we need to do the actual work, when it
		// tells us to
		//
		g_vccInstance->pfnUIUpdate = updateInterfaceCB;
		g_vccInstance->pfnScreenShot = screenShotCB;

		/*
		update the user interface
		*/
		if (g_vccInstance->pfnUIUpdate != NULL)
		{
			(*g_vccInstance->pfnUIUpdate)(g_vccInstance);
		}

		/*
		start the emulation thread - it was locked on creation
		*/
		SDL_UnlockMutex((SDL_mutex *)g_vccInstance->hEmuMutex);
	}

	// Main message loop:
	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_VCCUIWIN));
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}

////////////////////////////////////////////////////////////////////////////////////
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
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_VCCUIWIN));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_VCCUIWIN);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

////////////////////////////////////////////////////////////////////////////////////
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
HWND InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return NULL;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return hWnd;
}

////////////////////////////////////////////////////////////////////////////////////
//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
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

////////////////////////////////////////////////////////////////////////////////////
//
// Message handler for about box.
//
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

////////////////////////////////////////////////////////////////////////////////////
