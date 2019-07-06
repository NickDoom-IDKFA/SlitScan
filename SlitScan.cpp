#include "Watcom\Filter.h"
#include "Watcom\Watcom.h"

#define Inserted_As_CPP_Not_As_RC
#include "SlitScan.rc"
#endif	//Such a dirty hack lol

#include <mem.h>
#include <iostream.h>	//DEBUG ONLY!!!
#include <fstream.h>
#include <stdio.h>
//#include <stddef.h>

		//Like MS's interpretation of << extern "C" __declspec(dllexport) int __cdecl >> ("C" has the top priority == without "_" prefix)
		//unlike Watcom's interpretation of this (__cdecl has the top priority == with "_" prefix)                                       
#pragma aux __MScdeclC "*" parm caller [] value struct float struct routine [eax] modify [eax ecx edx];

typedef unsigned long uint32;
typedef unsigned short uint16;

//ToDo: instead of global, make all this shit local, multiplicable and relocable by writing a wrapper and using the magic Ctrl-X Ctrl-V tool.
//It's required to comply with VDub specifications in order to enable multiple plug-in instances per system.

uint32 SlitPositions=0;	//Number of slit positions.
uint32 SlitSizes[4096];	//Number of pixels in each of max. 4096 slit positions.
char *SlitPixels=NULL;	//Actual pixels of each slit, from slit 0 to slit N<4096. 24-bit compact form.
char *MonstroBuffer=NULL;
int FrameIndex, MainDelay=0;

HINSTANCE Hinst;

HBITMAP Preview;
BITMAPINFO Format={ sizeof(BITMAPINFOHEADER),0,0,1,24,BI_RGB,0,65535,65535,0,0};
HDC HdlReal=NULL,HdlCompat=NULL;
int MapW=0, MapH=0, TZX=0, TZY=0;
char *PreBoth=NULL;	//MapW and MapH should always be in sync with the real size of allocated memory, keep an eye on them!
char InfraredStylePalette[4096][3];
#define LEFT_P 0
#define TOP_P 100
#define MINW 750

FilterDefinition *g_registeredFilterDef;	
extern FilterDefinition myfilter_definition;

void DrawBitmap (HWND hWnd, char* Pixels)
{
	RECT rect;
	GetWindowRect(hWnd,&rect);
	MoveWindow(hWnd, rect.left, rect.top, max(LEFT_P+MapW+50,MINW), TOP_P+MapH+150, TRUE);

	if (Preview) DeleteObject(Preview);
	Format.bmiHeader.biWidth=MapW;
	Format.bmiHeader.biHeight=MapH;
	Preview=CreateCompatibleBitmap(HdlReal,MapW,MapH);
	SetDIBits(HdlCompat,Preview,0,MapH,Pixels,&Format,DIB_RGB_COLORS);
	SelectObject(HdlCompat,Preview);
	RedrawWindow(hWnd,NULL,NULL,RDW_INTERNALPAINT);
}

//где-то лежит ff, у которого в начале лежит указатель на функцию типа FilterDefinition, это сбивает при невнимательности.
//в fa в начале лежит указатель на саму структуру FilterDefinition, по которому ВДаб лазает (сам за руку ловил азмом).
//в edi лежал fa, из которого взяли FilterDefinition edx=edi[0], из которого взяли eax=edx[28h], в котором адрес Kernel
// сначала push 006af2dc (ff)		затем       push edi, в котором fa, по повадкам похоже на __cdecl
// call eax
/*int __cdecl DemoKernel  (const _FilterActivation *fa, const _FilterFunctions *ff)	//Add FILTERPARAM_SWAP_BUFFERS to test it instead of Kernel
{
	int           w        = fa->dst->w;
	int           h        = fa->dst->h;
	uint32       *dst      = (uint32 *)fa->dst->data;
	ptrdiff_t     dstpitch = fa->dst->pitch;
	const uint32 *src      = (const uint32 *)fa->src->data;
	ptrdiff_t     srcpitch = fa->dst->pitch;

cout<<"We've processed a frame! "<<&myfilter_definition<<" -> "<<g_registeredFilterDef<<endl;

//#pragma dump_object_model VFBitmap;

int i;
for (i=0;i<sizeof(FilterActivation)/4; i++) cout<<((int*)(fa))[i]<<" ";
cout<<endl;
cout<<fa->dst->h<<" "<<fa->dst->w<<" "<<fa->src->h<<" "<<fa->src->w<<endl;
for (i=0;i<20; i++) cout<<((int*)&(fa->dst))[i]<<" ";
cout<<endl;
//return 0;
	
	// loop over all rows
	for(int y=0; y<h; ++y)
	{
		// loop over all pixels in current row
		for(int x=0; x<w; ++x)
		{
		    uint32 pixelValue = src[x];
		    
		    // divide all channels by 2
		    dst[x] = (pixelValue & 0xfefefe) >> 1;
		}
	
		// advance to next row in destination and source pixmaps
		dst = (uint32 *)((char *)dst + dstpitch);
		src = (const uint32 *)((const char *)src + srcpitch);
	}
	return 0;
}*/
//add esp, 8	-- очистили стек от двух интов
//после выхода читаем только ebx, но он мог быть старый, а от нас могло требоваться сохранять регистры. Вероятно, в ebx не наш ответ, а ответ просто игнорится, ХЗ где он.


int __cdecl Kernel  (const _FilterActivation *fa, const _FilterFunctions *ff)
//I don't use "future image pre-request" feature because VDub itself can't magically made them from nothing. It'll cost either HUGE time (if VDub unpacks 100-200 frames for each frame processing)
//or HUGE RAM (if VDub pre-caches them, because it can't even drop used areas as I do). Simple "use a feature and don't care how it works" approach usually kills everything.
{
	int           w        = fa->dst->w;
	int           h        = fa->dst->h;
	uint32 *src      = (uint32 *)fa->src->data;
	ptrdiff_t     srcpitch = fa->dst->pitch;

	int Slit, Pixel, LinearIndex, X, Y, Int24, *FBuffPix;
	char *SlitPix, *BuffPix, *BuffSlitPix;

	if (!MapW||!MapH) return 0;	//No processing requested/set up.

	for(Y=0; Y<h && Y<MapH; Y++)
	{
		BuffPix=PreBoth+MapW*MapH*3 + Y*MapW*3;
		for(X=0; X<w && X<MapW; X++)
		{
			*BuffPix++ = src[X] & 0xFF;
			*BuffPix++ = src[X]>>8 & 0xFF;
			*BuffPix++ = src[X]>>16 & 0xFF;
		}	
		src = (uint32 *)((char *)src + srcpitch);
	}

	src = (uint32 *)fa->src->data;
	for (Slit=1,SlitPix = SlitPixels+SlitSizes[0]*3,BuffPix=MonstroBuffer; Slit<SlitPositions; BuffPix+=3*Slit*SlitSizes[Slit],Slit++)	//We bypass the zero-delay slit unless we have to copy it from src to dst.
	 for (Pixel=0,BuffSlitPix = BuffPix + 3*SlitSizes[Slit]*(FrameIndex%Slit); Pixel<SlitSizes[Slit]; Pixel++,SlitPix+=3,BuffSlitPix+=3)	//C is awesome :) It reads: for (all slits) for (all pixels of the slit) exchange_pixels(frame_slit_pixel, buffer_slit_pixel(actual_frame_index_with_proper_delay));
	 {
		LinearIndex=*((int*)(SlitPix))&0xFFFFFF;
		X=LinearIndex%MapW;
		Y=LinearIndex/MapW;
		if (Y<h && X<w)		//Map can be bigger than a frame -- it's useful for regular pattern maps.
		{
			FBuffPix=(int*)((char*)(src)+srcpitch*Y)+X;
			Int24=*((int*)(BuffSlitPix));
			*((int*)(BuffSlitPix)) = *FBuffPix&0xFFFFFF | Int24&0xFF000000;	//This plug-in does not process pixels, it simply stores are restores 24 bit pixels, so it probably won't be a problem to support any pixel format.
			*FBuffPix = Int24&0xFFFFFF;
//((int*)((char*)(src)+srcpitch*Y))[X]=Slit;
		}
	 }
	FrameIndex++;

	return 0;
}

uint16* ParseBitmap (char *Name, int IsPNG, int *D, int *B, int *A, int *F)
{
	uint16 *T, Px[4], *Map;
	fstream raw;
	int x,y;
	char *PreM, *PreI;

	if (IsPNG)
	{
//ToDo: attach png lib here, parse the .PNG as we parse .RAW but, of course, we must take actual MapW MapH D B from the file itself, not from user input, and overwrite the values of W H D B with values we get from png lib.
//Formats other than 1/4 channels 8/16 bpp (for example, common RGB888) must return NULL.
		return NULL;	//.PNG is not supported yet so nothing to return.
	}

	//Here we load the file. Source can be 1-channel (Grayscale representing our SlitScan Map only) and RGBA/ARGB (Alpha representing our SlitScan map and RGB for a preview/screenshot which allows user to select Zero Time Pixel easier).
	//Also, source can be 8 or 16 bpp. Target is Gray 16 bpp map + two RGB previews for the map (mandatory) and a screencap (can be empty).

	raw.open (Name, ios::in|ios::binary);
	T=(uint16*)malloc(MapW * MapH * 2);
	PreBoth = (char*) realloc (PreBoth, MapW * MapH * 6);

	Map=T;
	PreM=PreBoth;
	PreI=PreBoth + MapW*MapH*3;
	if (*D!=4) memset (PreI,0,MapW*MapH*3);

	for (x=y=0; y<MapH; x++,y+=x/MapW,x%=MapW)	//doubleindexed to simplify loop break logic
	{
		if (!(*F|x))	//manually patch addresses for each new string
		{
			Map =T			   +   MapW*(MapH-y-1);
			PreM=PreBoth		   + 3*MapW*(MapH-y-1);
			PreI=PreBoth + MapW*MapH*3 + 3*MapW*(MapH-y-1);
		}
		raw.read ((char*)Px,*B * *D);	//Watcom streams are cached very good :)
		if (raw.fail()) break;

		switch (*D + *B + *A * 8)
		{
			case 2:	//1 chan, 1 byte
			case 2+8:	//same but "ARGB" checked
				*Map++=Px[0]&0xFF;
				*PreM++=InfraredStylePalette[(Px[0]&0xFF)*16][0];
				*PreM++=InfraredStylePalette[(Px[0]&0xFF)*16][1];
				*PreM++=InfraredStylePalette[(Px[0]&0xFF)*16][2];
			continue;
			case 3:	//1 chan, 2 bytes
			case 3+8:	//same but "ARGB" checked
				if (Px[0]>4095) break;
				*Map++=Px[0];
				*PreM++=InfraredStylePalette[Px[0]][0];
				*PreM++=InfraredStylePalette[Px[0]][1];
				*PreM++=InfraredStylePalette[Px[0]][2];
			continue;

			case 5:	//4 chans, 1 byte
				*PreI++=Px[1]&0xFF;//B
				*PreI++=Px[0]>>8;//G
				*PreI++=Px[0]&0xFF;//R
				*Map++=Px[1]>>8;//A
				*PreM++=InfraredStylePalette[(Px[1]>>8)*16][0];
				*PreM++=InfraredStylePalette[(Px[1]>>8)*16][1];
				*PreM++=InfraredStylePalette[(Px[1]>>8)*16][2];
			continue;
			case 6:	//4 chans, 2 bytes
				if (Px[3]>4095) break;
				*PreI++=Px[2]>>8;//B
				*PreI++=Px[1]>>8;//G
				*PreI++=Px[0]>>8;//R
				*Map++=Px[3];//A
				*PreM++=InfraredStylePalette[Px[3]][0];
				*PreM++=InfraredStylePalette[Px[3]][1];
				*PreM++=InfraredStylePalette[Px[3]][2];
			continue;

			case 5+8:	//4 chans, 1 byte, ARGB
				*PreI++=Px[1]>>8;//B
				*PreI++=Px[1]&0xFF;//G
				*PreI++=Px[0]>>8;//R
				*Map++=Px[0]&0xFF;//A
				*PreM++=InfraredStylePalette[(Px[0]&0xFF)*16][0];
				*PreM++=InfraredStylePalette[(Px[0]&0xFF)*16][1];
				*PreM++=InfraredStylePalette[(Px[0]&0xFF)*16][2];
			continue;
			case 6+8:	//4 chans, 2 bytes, ARGB
				if (Px[0]>4095) break;
				*PreI++=Px[3]>>8;//B
				*PreI++=Px[2]>>8;//G
				*PreI++=Px[1]>>8;//R
				*Map++=Px[0];//A
				*PreM++=InfraredStylePalette[Px[0]][0];
				*PreM++=InfraredStylePalette[Px[0]][1];
				*PreM++=InfraredStylePalette[Px[0]][2];
			continue;

			default:
			break;
		}
		//We get here only on errors:
		free(T);
		T=NULL;
		MapW=MapH=0;
		break;
	}
	raw.close();

	return T;
}

int ThirdInt(const void *op1, const void *op2)
{
	const int *i1=(const int*) op1;
	const int *i2=(const int*) op2;
	if (i1[2]==i2[2]) return (i1[1]<<16|i1[0]) - (i2[1]<<16|i2[0]);	//In the same slit, pixels are sorted as they are placed in RAM to save some cache speed.
	return i1[2]-i2[2];
}

void Button (int CtlNum, HWND hWnd)
{
	short *Temp=NULL;
	char Name[_MAX_PATH]="";
	int D, B, A, F;
	int x, y, *List;
	unsigned long Size;

	switch (CtlNum)
	{
		case LOADMAP:
			OPENFILENAME MapName={0};
			MapName.lStructSize = sizeof(OPENFILENAME);
			MapName.hwndOwner = hWnd;
			MapName.lpstrFilter = "RAW Image file\0*.RAW\0Portable Network Graphics file\0*.PNG\0\0";
			MapName.lpstrFile = Name;
			MapName.nMaxFile = 512;
			MapName.Flags = OFN_FILEMUSTEXIST|OFN_EXPLORER;
			MapName.lpstrDefExt="RAW";
			MapName.lpTemplateName="";
			if (!GetOpenFileName(&MapName)) return;

			MapW=GetDlgItemInt(hWnd, RAWW, &A ,FALSE);
			MapH=GetDlgItemInt(hWnd, RAWH, &B ,FALSE);
			if (!A||!B) MapW=MapH=0;
			B=1 +   (IsDlgButtonChecked(hWnd, TWOBYTE) == BST_CHECKED);
			D=1 + 3*(IsDlgButtonChecked(hWnd, RGB    ) == BST_CHECKED);
			A=      (IsDlgButtonChecked(hWnd, ARGB   ) == BST_CHECKED);
			F=      (IsDlgButtonChecked(hWnd, RAWFLIP) == BST_CHECKED);
			
			if (MapW*MapH>0 && MapW*MapH<0x1000000) Temp=ParseBitmap (Name, 0, &D, &B, &A, &F);
			else MapW=MapH=0;

			if (!Temp || !MapW || !MapH)
			{
				MessageBox( hWnd,"Couldn't read the file!","Error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
				return;
			}

			SetDlgItemInt(hWnd, RAWW, MapW ,FALSE);	//For future .PNG support: inform user about actual file format.
			SetDlgItemInt(hWnd, RAWH, MapH ,FALSE);
			CheckDlgButton (hWnd, TWOBYTE, (B==2));
			CheckDlgButton (hWnd, RGB    , (D==4));
			CheckDlgButton (hWnd, ARGB   , (A==1));
			CheckDlgButton (hWnd, RAWFLIP, (F==1));

			List=(uint32*)malloc(MapW*MapH*3*sizeof(uint32));
			for (x=0;x<MapW;x++)
			 for (y=0;y<MapH;y++)
			 {
				List[x*3+y*MapW*3  ]=x;
				List[x*3+y*MapW*3+1]=y;
				List[x*3+y*MapW*3+2]=Temp[x+y*MapW];
			 }			//List: X, Y, Depth, X, Y, Depth... H*W records, one per pixel.
			qsort (List, MapW*MapH, sizeof(uint32)*3, ThirdInt);	//Now sorted from "no delay" pixels to "max delay" ones.
//for (x=0;x<W*H;x++) cout<<List[x*3]<<" x "<<List[x*3+1]<<" -> "<<List[x*3+2]<<endl;
			SlitPixels = (char*)realloc(SlitPixels,MapW*MapH*3+1);	//Last byte is needed to simplify overlapped write;
			memset (SlitSizes,0,sizeof(SlitSizes));
			for (x=0; x<MapW*MapH; x++) SlitSizes[List[x*3+2]]++;	//How many pixels of each delay we have.
			SlitPositions = List[MapW*MapH*3-1] + 1;		//Last record is obvoiusly "max delay" one.
//cout<<"SlitPositions: "<<SlitPositions<<endl;
				//Now place XxY coords of each pixel record into the compact map, using the compact 24-bit linear address.
			for (x=0; x<MapW*MapH; x++) *((uint32*)(SlitPixels+x*3))=List[x*3]+List[x*3+1]*MapW;	//Warning! This works only with Intel byte order!
//int i; for (y=i=0;y<SlitPositions;y++) for (x=0;x<SlitSizes[y];x++,i++) cout<<(*((int*)(SlitPixels+i*3))&0xFFFFFF)%640<<" x "<<(*((int*)(SlitPixels+i*3))&0xFFFFFF)/640<<" -> "<<y<<endl;
			
//for (x=0; x<SlitPositions; x++) cout<<SlitSizes[x]<<endl;
			for (x=1,Size=0; x<SlitPositions; x++) Size+=SlitSizes[x]*x;	//Pixels with delay 0 require no memory to store, ones with delay 1 require 1xRGB, ones with delay 2 require 2xRGB (in order to be kept for two frames) etc.
			free (List);
			free (Temp);

//cout<<"Allocating "<<Size*3+1<<endl;
		//ToDo: Should use less restricted memory allocator here! This one failed even on a "tiny" 1.5 Gb array.
			MonstroBuffer=(char*)realloc(MonstroBuffer, Size*3+1);	//overlapping int32 write as usually
			if (!MonstroBuffer)
			{
				MessageBox( hWnd,"Sorry! Can't allocate memory for the timeline buffer.","Error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
				SlitPositions=0;
			}
			else memset (MonstroBuffer, FrameIndex=0, Size*3+1);	//ToDo: ALSO CLEAR BUFFER BEFORE 1st frame!!! Users will damn you if you'll allow a prev video to get into a next video.

	//	break; Nope! Draw new Map, of course.
		case PREM:
			DrawBitmap (hWnd, PreBoth);
		break;
		case PREV:
			DrawBitmap (hWnd, PreBoth+MapW*MapH*3);
		break;
		case IDOK:
			TZX=GetDlgItemInt(hWnd, ZEROX, &A ,FALSE);
			TZY=GetDlgItemInt(hWnd, ZEROY, &B ,FALSE);
			if (!A||!B) return;
			for (y=A=0;y<SlitPositions;y++)	//Structure is optimised for fast frame processing, not for fast interface, so we just gonna find an item via simple loop.
			 for (x=0;x<SlitSizes[y];x++,A++)
			  if ( (*((int*)(SlitPixels+A*3))&0xFFFFFF) == TZX + TZY*MapW )
			   MainDelay=y;
			sprintf (Name, "Set Zero Time frame %i now", MainDelay);
			SetDlgItemText (hWnd, IDOK, Name);
			    //Mayakovsky rulezzz ;)
		break;
		case IDCANCEL:
			SendMessage (hWnd,WM_CLOSE,0,0);
		break;
	}
}

BOOL __export PASCAL StdWinDialog (HWND hWnd, unsigned uMsg, WPARAM wParam, LPARAM lParam)
{
	int i,j,h,k;
	switch (uMsg)
	{
		case WM_INITDIALOG:
			HdlReal=GetDC(hWnd);
			HdlCompat=CreateCompatibleDC(HdlReal);
			SetDlgItemInt(hWnd, RAWW, MapW ,FALSE);
			SetDlgItemInt(hWnd, RAWH, MapH ,FALSE);
			SetDlgItemInt(hWnd,ZEROX, TZX  ,FALSE);
			SetDlgItemInt(hWnd,ZEROY, TZY  ,FALSE);
		return FALSE;

		case WM_CLOSE:
			ReleaseDC(hWnd,HdlCompat);
			ReleaseDC(hWnd,HdlReal);
			if (Preview) DeleteObject(Preview);
			Preview=NULL;
			EndDialog (hWnd,NULL);
		return FALSE;

		case WM_PAINT:
			BitBlt(HdlReal,LEFT_P,TOP_P,Format.bmiHeader.biWidth,Format.bmiHeader.biHeight,HdlCompat,0,0,SRCCOPY);
		return FALSE;

		case WM_LBUTTONUP:
// DEBUG %)	case WM_MOUSEMOVE:
			i=(lParam&0xFFFF)-LEFT_P;
			j=MapH-(lParam>>16)+TOP_P;
			if (i<0 || j<0 || i>=MapW || j>=MapH) return FALSE;

			SetDlgItemInt (hWnd,ZEROX,i,FALSE);
			SetDlgItemInt (hWnd,ZEROY,j,FALSE);
			Button (IDOK,hWnd);
		return FALSE;

		case WM_COMMAND:
	                if (lParam && wParam>>16==BN_CLICKED) Button (wParam&0xFFFF,hWnd);
	                else return FALSE;
		break;

		default:
		return FALSE;
        }
        return TRUE;
}

//int __cdecl DialogWindow (VDXFilterActivation *fa, const VDXFilterFunctions *ff, HWND hWnd)
//int __cdecl DialogWindow (void *fa, const void *ff, HWND hWnd)
int __cdecl DialogWindow (const _FilterActivation *fa, const _FilterFunctions *ff, HWND hWnd)
{
//	MessageBox( hWnd,__func__,"Dialog", MB_OK | MB_TASKMODAL);

	DialogBoxParam(Hinst,"SLITSCAN",hWnd,StdWinDialog,NULL);

//	cout<<"Parameters has been CHANGED!!! "<<&myfilter_definition<<" -> "<<g_registeredFilterDef<<endl;

	return 0;
}

int __cdecl Parameters  (const _FilterActivation *fa, const _FilterFunctions *ff)
{	
//	cout<<"We've been asked for parameters! Safe to process now. "<<&myfilter_definition<<" -> "<<g_registeredFilterDef<<endl;
//	return FILTERPARAM_SWAP_BUFFERS | FILTERPARAM_HAS_LAG(32);
	return FILTERPARAM_HAS_LAG(MainDelay);
}

FilterDefinition myfilter_definition={
	0,0,NULL,                       // next, prev, and module (set to zero)
	"SlitScan",                    // name
	"SlitScan (variable timeline delay) plug-in with freeform map support.\r\nBETA restrictions: does not clear non-existing areas in first couple of frames; allows only one instance per system; supports only .RAW map format; can't allocate enough Gb for really deep timeline shifts; only RGB888 frames are supported; \"IR Camera\" palette not done yet",    // description
	"NickDoom the Industrial Programmer",         // author / maker
	NULL,                           // no private data
	0,                              // no instance data size
	NULL,                           // no initProc
	NULL,                           // no deinitProc
	(FilterRunProc)(Kernel),                   // runProc
	(FilterParamProc)(Parameters),
	(FilterConfigProc)(DialogWindow)
	// allow the compiler to zero-initialize the rest of the fields
};

//extern "C" __declspec(dllexport) int __cdecl VirtualdubFilterModuleInit2(struct FilterModule *fm, const FilterFunctions *ff, int& vdfd_ver, int& vdfd_compat)
extern "C" __declspec(dllexport) int VirtualdubFilterModuleInit2(struct FilterModule *fm, const FilterFunctions *ff, int& vdfd_ver, int& vdfd_compat)
{
	int i;
	//ToDo: fill it with less idiotic palette.
	for (i=0;i<4096;i++) InfraredStylePalette[i][0]=i/16;	//Blue
	for (i=0;i<4096;i++) InfraredStylePalette[i][1]=i;	//Green
	for (i=0;i<4096;i++) InfraredStylePalette[i][2]=i*16;	//Red


//	if (MessageBox( NULL,__func__,"Ta-da!", MB_YESNO | MB_TASKMODAL) == IDYES)
	g_registeredFilterDef = ff->addFilter(fm, &myfilter_definition, sizeof(FilterDefinition));
	
//if (g_registeredFilterDef) MessageBox( NULL,__func__,"We got it!", MB_OK | MB_TASKMODAL);
//cout<<"Added "<<&myfilter_definition<<" -> "<<g_registeredFilterDef<<endl;

	vdfd_ver        = VIRTUALDUB_FILTERDEF_VERSION;
	vdfd_compat     = VIRTUALDUB_FILTERDEF_COMPATIBLE;    // we need this version for copy constructor support
	
	return 0;
}
#pragma aux (__MScdeclC) VirtualdubFilterModuleInit2;


//extern "C" __declspec(dllexport) void __cdecl VirtualdubFilterModuleDeinit(struct FilterModule *fm, const FilterFunctions *ff)
extern "C" __declspec(dllexport) void VirtualdubFilterModuleDeinit(struct FilterModule *fm, const FilterFunctions *ff)
{
	// Note: This must be the pointer returned from addFilter(), NOT your original
	// declaration.
//	if (MessageBox( NULL,__func__,"Ta-da!", MB_YESNO | MB_TASKMODAL) == IDYES)

	free (SlitPixels);
	free (MonstroBuffer);
	free (PreBoth);
	SlitPixels=NULL;
	MonstroBuffer=NULL;
	PreBoth=NULL;
	SlitPositions=0;
	MapW=MapH=0;

	ff->removeFilter(g_registeredFilterDef);

//cout<<"Removed "<<&myfilter_definition<<" -> "<<g_registeredFilterDef<<endl;
}
#pragma aux (__MScdeclC) VirtualdubFilterModuleDeinit;

BOOL WINAPI DllMain (HINSTANCE h, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason==DLL_PROCESS_ATTACH) Hinst=h;
        return 1;
}