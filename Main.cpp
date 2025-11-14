#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <cstdlib>      
#include <cstring>      


//le path et le texte
HWND g_hPathControl = NULL;
HWND g_hMessageControl = NULL;

//data de l'image par default
HBITMAP g_hBaseOriginalBitmap = NULL;
HDC     g_hBaseOriginalMemoryDC = NULL;
long    g_BaseImageWidth = 0;
long    g_BaseImageHeight = 0;

//data pour l'image du user
HBITMAP g_hOriginalBitmap = NULL;
HDC     g_hOriginalMemoryDC = NULL;
long    g_ImageWidth = 0;
long    g_ImageHeight = 0;

//data de l'image modifier
HBITMAP g_hModiflBitmap = NULL;
HDC     g_hModifMemoryDC = NULL;
long    g_ModifImageWidth = 0;
long    g_ModifImageHeight = 0;

std::vector<BYTE> loadFileToBuffer(const std::wstring& filename) {

    // Ouvre le fichier en mode binaire et se positionne à la fin
    std::ifstream file(filename, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        std::wcerr << L"ERREUR: Impossible d'ouvrir le fichier " << filename << std::endl;
        return {};
    }
    //tellg renvoie le nb d'octet entre le curseur et le depart, donc la taille si on est a la fin
    std::streamsize size = file.tellg();

    //on se met au debut
    file.seekg(0, std::ios::beg);
    //on passe tout dans le buffer
    std::vector<BYTE> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return buffer;
    }

    return {};
}

BYTE* LoadBMP(BYTE* buffer, int size, long& m_width, long& m_height)
{
    if (buffer == nullptr || size <= 0)
        return nullptr;

    BYTE* src = buffer;
    //on prend les info du header
    BITMAPFILEHEADER bfh;
    memcpy(&bfh, src, sizeof(BITMAPFILEHEADER));
    src += sizeof(BITMAPFILEHEADER);
    // 0x4D42 = "BM" c'est la signature du bmp, si elle est pas la, c'est pas un vrai .bmp
    if (bfh.bfType != 0x4D42) // "BM"
        return nullptr;

    //on prend les info
    BITMAPINFOHEADER bih;
    memcpy(&bih, src, sizeof(BITMAPINFOHEADER));
    src = buffer + bfh.bfOffBits;
    if (bih.biBitCount != 24 && bih.biBitCount != 32)
        return nullptr;

    m_width = bih.biWidth;
    m_height = abs(bih.biHeight);
    //abs car deux facons d'enregistrer un fichier, < 0 pour de bas en haut, > 0 pour de haut en bas
    if (m_width == 0 || m_height == 0)
        return nullptr;

    //nb de bit par pixel
    long m_bits = bih.biBitCount;
    //sa c'est compliquer je t'expliquerais en vrai
    int stride = ((((m_width * m_bits) + 31) & ~31) >> 3);
    //on calcule la taille d'une ligne, x4 pour R, G, B, A
    long m_stride = m_width * 4; 
    //taille totale
    long m_size = m_stride * m_height;

    // Alloue le buffer de destination (RGBA 32 bits)
    BYTE* m_rgba = new (std::nothrow) BYTE[m_size];
    if (m_rgba == nullptr) return nullptr;

    for (int y = 0; y < m_height; y++)
    {
        BYTE* trg = m_rgba; 
        // gere le sens (haut en bas ou bas en haut)
        if (bih.biHeight < 0)
            trg += y * m_stride;
        else
            trg += m_size - (y + 1) * m_stride;

        BYTE* cur = src; // Pointeur source

        for (int x = 0; x < m_width; x++)
        {
            // Conversion BGR (source) -> RGB (destination)
            trg[0] = cur[0]; // R
            trg[1] = cur[1]; // G
            trg[2] = cur[2]; // B

            //si on est sur 32 alors y'a de la transparence
            if (m_bits == 32) {
                trg[3] = cur[3]; // A
                cur += 4;
            }
            else { // sinon est pas transparent
                trg[3] = 255; 
                cur += 3;
            }
            trg += 4; 
        }
        src += stride;
    }
    return m_rgba; 
}


void PrepareImageForDrawing(HDC hdcRef, BYTE* rgba_data, HBITMAP& hBitmapOut, HDC& hDCOut, long width, long height)
{
    //la ou on va ecrire
    hDCOut = CreateCompatibleDC(hdcRef);

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; 
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;

    LPVOID pBits;
    hBitmapOut = CreateDIBSection(hDCOut, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);

    // on passe les data
    memcpy(pBits, rgba_data, width * height * 4);

    // on va ecrire dans HDCOut HBitmapOut JUSTE APRES
    SelectObject(hDCOut, hBitmapOut);
}


void ReadTextFromInput(HWND hwnd) {
    if (g_hPathControl == NULL) return;

    //on recup la taille de ce qui est ecris
    int length = GetWindowTextLength(g_hPathControl);


    if (length > 0) {
        std::vector<wchar_t> buffer(length + 1);
        //on recupe ce qui est ecris et on le met dans le buffer
        GetWindowText(g_hPathControl, buffer.data(), length + 1);

        //on supprime ce qui est la avant (au cas ou le mec change d'image)
        if (g_hOriginalBitmap) {
            DeleteObject(g_hOriginalBitmap);
            DeleteDC(g_hOriginalMemoryDC);
        }

        // on passe les datas
        std::vector<BYTE> bmp_buffer = loadFileToBuffer(buffer.data());

        if (bmp_buffer.empty()) {
            MessageBox(hwnd, L"Impossible de charger l'image. Affichage de l'image par défaut.", L"Erreur de Fichier", MB_OK);
            g_hOriginalBitmap = NULL;
        }
        else {
            //on traite les data brut
            BYTE* rgba_data = LoadBMP(bmp_buffer.data(), bmp_buffer.size(), g_ImageWidth, g_ImageHeight);

            if (rgba_data != nullptr) {
                HDC hdcScreen = GetDC(NULL);
                //ici on a selectionner notre image
                PrepareImageForDrawing(hdcScreen, rgba_data, g_hOriginalBitmap, g_hOriginalMemoryDC, g_ImageWidth, g_ImageHeight);
                ReleaseDC(NULL, hdcScreen);
                //on suppr car c'est deja dans l'util selectionner donc plus besoins de ce vector
                //faut mettre [] par ce que c'est un vector
                delete[] rgba_data;
            }
        }

        //on redesine
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

//ici on gere TOUT les comportement de notre app
LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {

    case WM_CREATE: 
    {
        //a la creation, une seul fois.
        HINSTANCE hInstance = ((LPCREATESTRUCT)lParam)->hInstance;

        //textes et lignes
        CreateWindowEx(0, L"STATIC", L"Image Before Modif :", WS_CHILD | WS_VISIBLE | SS_CENTER, 50, 50, 160, 20, hwnd, NULL, hInstance, NULL);
        CreateWindowEx(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ETCHEDVERT, 375, 50, 3, 200, hwnd, NULL, hInstance, NULL);
        CreateWindowEx(0, L"STATIC", L"Image After Modif :", WS_CHILD | WS_VISIBLE | SS_CENTER, 500, 50, 160, 20, hwnd, NULL, hInstance, NULL);
        CreateWindowEx(0, L"STATIC", L"Enter Path To Image.bmp :", WS_CHILD | WS_VISIBLE | SS_CENTER, 50, 275, 170, 20, hwnd, NULL, hInstance, NULL);
        CreateWindowEx(0, L"STATIC", L"Enter Message To Hide :", WS_CHILD | WS_VISIBLE | SS_CENTER, 50, 375, 160, 20, hwnd, NULL, hInstance, NULL);

        //pour le chemin d'acces
        g_hPathControl = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER, 50, 300, 375, 30, hwnd, (HMENU)1000, hInstance, NULL);


        //pour le text a cacher
        g_hMessageControl = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER, 50, 400, 700, 90, hwnd, (HMENU)1010, hInstance, NULL);

        //les buttons
        CreateWindowEx(0, L"BUTTON", L"Use this Path", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 550, 295, 100, 30, hwnd, (HMENU)1001, hInstance, NULL);
        CreateWindowEx(0, L"BUTTON", L"Show Preview", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 100, 515, 150, 30, hwnd, (HMENU)1111, hInstance, NULL);
        CreateWindowEx(0, L"BUTTON", L"Download Modif Image", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 550, 515, 150, 30, hwnd, (HMENU)0111, hInstance, NULL);

        break;
    }

    case WM_COMMAND: 
    {
        //quand on click sur un btn
        int controlId = LOWORD(wParam);

        if (controlId == 1001) //id du boutton Use This Path
        {
            ReadTextFromInput(hwnd);
        }
    }
    break;

    case WM_PAINT: // Dessin de la fenêtre
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        //image 1

        // si l'user a mit une image, sinon celle par default
        HBITMAP hBmpToDraw = (g_hOriginalBitmap != NULL) ? g_hOriginalBitmap : g_hBaseOriginalBitmap;
        HDC hDCToDraw = (g_hOriginalBitmap != NULL) ? g_hOriginalMemoryDC : g_hBaseOriginalMemoryDC;
        long imgW = (g_hOriginalBitmap != NULL) ? g_ImageWidth : g_BaseImageWidth;
        long imgH = (g_hOriginalBitmap != NULL) ? g_ImageHeight : g_BaseImageHeight;

        if (hBmpToDraw != NULL) {
            // StrechBlt redimentionne l'image pour que ca fasse beau (meme si c'est un peu moche)
            StretchBlt(
                hdc, 50, 80, 300, 200,
                hDCToDraw, 0, 0, imgW, imgH,
                SRCCOPY
            );
        }

        if (g_hModiflBitmap != NULL) {
            //si jamais les data sont pas nul, y'a eu un truc, on l'affiche
            StretchBlt(hdc, 450, 80, 300, 200,
                g_hModifMemoryDC, 0, 0, g_ModifImageWidth, g_ModifImageHeight,
                SRCCOPY);
        }

        EndPaint(hwnd, &ps);
    }
    break;

    case WM_DESTROY: 
        // On Supprime tout
        DeleteObject(g_hBaseOriginalBitmap);
        DeleteDC(g_hBaseOriginalMemoryDC);
        DeleteObject(g_hOriginalBitmap);
        DeleteDC(g_hOriginalMemoryDC);
        DeleteObject(g_hModiflBitmap);
        DeleteDC(g_hModifMemoryDC);

        PostQuitMessage(0);
        break;

    default:
        //pour tout les autre message, on laisse le comportement de base de window
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

WNDCLASSEX initWindowClass(HINSTANCE hInstance, WNDPROC procedure, LPCWSTR className) {
    //fonction pour faire sa propre mais pas necessaire
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW; // on redessine si y'a redimention
    wc.lpfnWndProc = procedure;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); // cursor de base
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // fond
    wc.lpszClassName = className;
    return wc;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR LpCmdLine, int nCmdShow) {


    WNDCLASSEX wc = initWindowClass(hInstance, WindowProcedure, L"FirstWindow");

    //on enregistre la classe pour que Window la connaisse, sinon on peut pas cree une fenetre "modée"
    if (!RegisterClassEx(&wc)) {
        return 1;
    }

    //on cree notre fenetre modee
    HWND  hwnd = CreateWindowEx(
        0, L"FirstWindow", L"Projet de Stéganographie",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) {
        MessageBox(NULL, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    //image de base
    HDC hdcScreen = GetDC(NULL);

    std::vector<BYTE> bmp_buffer = loadFileToBuffer(L"BaseImage.bmp");

    if (!bmp_buffer.empty()) {
        BYTE* rgba_data = LoadBMP(bmp_buffer.data(), bmp_buffer.size(), g_BaseImageWidth, g_BaseImageHeight);
        if (rgba_data != nullptr) {
            PrepareImageForDrawing(hdcScreen, rgba_data, g_hBaseOriginalBitmap, g_hBaseOriginalMemoryDC, g_BaseImageWidth, g_BaseImageHeight);
            delete[] rgba_data;
        }
        else
            MessageBox(hwnd, L"les Datas de BaseImage.bmp sont incorrectes", L"Erreur de Fichier", MB_OK);
    }
    else
        MessageBox(hwnd, L"BaseImage.bmp n'a pas pu etre charger", L"Erreur de Fichier", MB_OK);


    ReleaseDC(NULL, hdcScreen);

    //on affiche tout une premiere fois
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = { 0 };
    //boucle de l'app, c'est la qu'on prend les inputs
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}