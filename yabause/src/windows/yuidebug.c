/*  Copyright 2004 Guillaume Duhamel
    Copyright 2004-2008 Theo Berkau
    Copyright 2005 Joost Peters

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include <windows.h>
#include <commctrl.h>
#include "resource.h"
#undef FASTCALL
#include "../memory.h"
#include "../scu.h"
#include "../sh2d.h"
#include "../vdp2debug.h"
#include "../yui.h"
#include "disasm.h"
#include "hexedit.h"
#include "settings.h"
#include "yuidebug.h"

extern HINSTANCE y_hInstance;
extern HWND YabWin;

u32 mtrnssaddress=0x06004000;
u32 mtrnseaddress=0x06100000;
char mtrnsfilename[MAX_PATH] = "\0";
int mtrnsreadwrite=1;
int mtrnssetpc=TRUE;

u32 memaddr=0;

SH2_struct *debugsh;

u32 *vdp1texture=NULL;
int vdp1texturew, vdp1textureh;

addrlist_struct hexaddrlist[13] = {
   { 0x00000000, 0x0007FFFF, "Bios ROM" },
   { 0x00180000, 0x0018FFFF, "Backup RAM" },
   { 0x00200000, 0x002FFFFF, "Low Work RAM" },
   { 0x02000000, 0x03FFFFFF, "A-bus CS0" },
   { 0x04000000, 0x04FFFFFF, "A-bus CS1" },
   { 0x05000000, 0x057FFFFF, "A-bus Dummy" },
   { 0x05800000, 0x058FFFFF, "A-bus CS2" },
   { 0x05A00000, 0x05AFFFFF, "68k RAM" },
   { 0x05C00000, 0x05C7FFFF, "VDP1 RAM" },
   { 0x05C80000, 0x05CBFFFF, "VDP1 Framebuffer" },
   { 0x05E00000, 0x05E7FFFF, "VDP2 RAM" },
   { 0x05F00000, 0x05F00FFF, "VDP2 Color RAM" },
   { 0x06000000, 0x060FFFFF, "High Work RAM" }
};

HWND LogWin=NULL;
char *logbuffer;
u32 logcounter=0;
u32 logsize=512;

//////////////////////////////////////////////////////////////////////////////

void SetupOFN(OPENFILENAME *ofn, int type, HWND hwnd, const LPCTSTR lpstrFilter, char *lpstrFile, DWORD nMaxFile)
{
   ZeroMemory(ofn, sizeof(OPENFILENAME));
   ofn->lStructSize = sizeof(OPENFILENAME);
   ofn->hwndOwner = hwnd;
   ofn->lpstrFilter = lpstrFilter;
   ofn->nFilterIndex = 1;
   ofn->lpstrFile = lpstrFile;
   ofn->nMaxFile = nMaxFile;

   switch (type)
   {
      case OFN_DEFAULTSAVE:
      {
         ofn->Flags = OFN_OVERWRITEPROMPT;
         break;
      }
      case OFN_DEFAULTLOAD:
      {
         ofn->Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
         break;
      }
   }

}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK ErrorDebugDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                   LPARAM lParam)
{
   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         SetDlgItemText(hDlg, IDC_EDTEXT, _16(lParam));
         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDC_EDCONTINUE:
            {
               EndDialog(hDlg, FALSE);

               return TRUE;
            }
            case IDC_EDDEBUG:
            {
               EndDialog(hDlg, TRUE);

               return TRUE;
            }
            default: break;
         }
         break;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

void YuiErrorMsg(const char *string)
{
   extern SH2Interface_struct *SH2Core;

   // This sucks, but until YuiErrorMsg is changed around, this will have to do
   if (strncmp(string, "Master SH2 invalid opcode", 25) == 0)
   {
      if (SH2Core->id == SH2CORE_DEBUGINTERPRETER)
      {
         if (DialogBoxParam(y_hInstance, MAKEINTRESOURCE(IDD_ERRORDEBUG), NULL, (DLGPROC)ErrorDebugDlgProc, (LPARAM)string) == TRUE)
         {
            debugsh = MSH2;
            DialogBox(y_hInstance, MAKEINTRESOURCE(IDD_SH2DEBUG), NULL, (DLGPROC)SH2DebugDlgProc);
         }
      }
   }
   else if (strncmp(string, "Slave SH2 invalid opcode", 24) == 0)
   {
      if (SH2Core->id == SH2CORE_DEBUGINTERPRETER)
      {
         if (DialogBoxParam(y_hInstance, MAKEINTRESOURCE(IDD_ERRORDEBUG), NULL, (DLGPROC)ErrorDebugDlgProc, (LPARAM)string) == TRUE)
         {
            debugsh = SSH2;
            DialogBox(y_hInstance, MAKEINTRESOURCE(IDD_SH2DEBUG), NULL, (DLGPROC)SH2DebugDlgProc);
         }
      }
   }
   else
      MessageBox (YabWin, _16(string), _16("Error"),  MB_OK | MB_ICONINFORMATION);
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK MemTransferDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                    LPARAM lParam)
{
   char tempstr[256];

   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         SetDlgItemText(hDlg, IDC_EDITTEXT1, _16(mtrnsfilename));

         sprintf(tempstr, "%08X", (int)mtrnssaddress);
         SetDlgItemText(hDlg, IDC_EDITTEXT2, _16(tempstr));

         sprintf(tempstr, "%08X", (int)mtrnseaddress);
         SetDlgItemText(hDlg, IDC_EDITTEXT3, _16(tempstr));

         if (mtrnsreadwrite == 0)
         {
            SendMessage(GetDlgItem(hDlg, IDC_DOWNLOADMEM), BM_SETCHECK, BST_CHECKED, 0);
            SendMessage(GetDlgItem(hDlg, IDC_UPLOADMEM), BM_SETCHECK, BST_UNCHECKED, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_EDITTEXT3), TRUE);
            EnableWindow(GetDlgItem(hDlg, IDC_CHECKBOX1), FALSE);
         }
         else
         {
            SendMessage(GetDlgItem(hDlg, IDC_DOWNLOADMEM), BM_SETCHECK, BST_UNCHECKED, 0);
            SendMessage(GetDlgItem(hDlg, IDC_UPLOADMEM), BM_SETCHECK, BST_CHECKED, 0);
            if (mtrnssetpc)
               SendMessage(GetDlgItem(hDlg, IDC_CHECKBOX1), BM_SETCHECK, BST_CHECKED, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_EDITTEXT3), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_CHECKBOX1), TRUE);
         }

         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDC_BROWSE:
            {
               OPENFILENAME ofn;

               if (SendMessage(GetDlgItem(hDlg, IDC_DOWNLOADMEM), BM_GETCHECK, 0, 0) == BST_CHECKED)
               {
                  WCHAR filter[1024];

                  CreateFilter(filter, 1024,
                     "All files (*.*)", "*.*",
                     "Binary Files", "*.BIN", NULL);

                  SetupOFN(&ofn, OFN_DEFAULTSAVE, hDlg, filter,
                           mtrnsfilename, sizeof(mtrnsfilename));

                  if (GetSaveFileName(&ofn))
                     SetDlgItemText(hDlg, IDC_EDITTEXT1, _16(mtrnsfilename));
               }
               else
               {
                  WCHAR filter[1024];

                  CreateFilter(filter, 1024,
                     "All files (*.*)", "*.*",
                     "Binary Files", "*.BIN",
                     "COFF Files", "*.COF;*.COFF", NULL);

                  // setup ofn structure
                  SetupOFN(&ofn, OFN_DEFAULTLOAD, hDlg, filter,
                           mtrnsfilename, sizeof(mtrnsfilename));

                  if (GetOpenFileName(&ofn))
                     SetDlgItemText(hDlg, IDC_EDITTEXT1, _16(mtrnsfilename));
               }

               return TRUE;
            }
            case IDOK:
            {
               WCHAR tempwstr[MAX_PATH];
               GetDlgItemText(hDlg, IDC_EDITTEXT1, tempwstr, MAX_PATH);
               WideCharToMultiByte(CP_ACP, 0, tempwstr, -1, mtrnsfilename, MAX_PATH, NULL, NULL);

               GetDlgItemText(hDlg, IDC_EDITTEXT2, tempwstr, 9);
               WideCharToMultiByte(CP_ACP, 0, tempwstr, -1, tempstr, MAX_PATH, NULL, NULL);
               sscanf(tempstr, "%08lX", &mtrnssaddress);

               GetDlgItemText(hDlg, IDC_EDITTEXT3, tempwstr, 9);
               WideCharToMultiByte(CP_ACP, 0, tempwstr, -1, tempstr, MAX_PATH, NULL, NULL);
               sscanf(tempstr, "%08lX", &mtrnseaddress);

               if ((mtrnseaddress - mtrnssaddress) < 0)
               {
                  MessageBox (hDlg, _16("Invalid Start/End Address Combination"), _16("Error"),  MB_OK | MB_ICONINFORMATION);
                  EndDialog(hDlg, TRUE);
                  return FALSE;
               }

               if (SendMessage(GetDlgItem(hDlg, IDC_DOWNLOADMEM), BM_GETCHECK, 0, 0) == BST_CHECKED)
               {
                  // Let's do a ram dump
                  MappedMemorySave(mtrnsfilename, mtrnssaddress, mtrnseaddress - mtrnssaddress);
                  mtrnsreadwrite = 0;
               }
               else
               {
                  // upload to ram and possibly execute
                  mtrnsreadwrite = 1;

                  // Is this a program?
                  if (SendMessage(GetDlgItem(hDlg, IDC_CHECKBOX1), BM_GETCHECK, 0, 0) == BST_CHECKED)
                  {
                     MappedMemoryLoadExec(mtrnsfilename, mtrnssaddress);
                     mtrnssetpc = TRUE;
                  }
                  else
                  {
                     MappedMemoryLoad(mtrnsfilename, mtrnssaddress);
                     mtrnssetpc = FALSE;
                  }
               }

               EndDialog(hDlg, TRUE);

               return TRUE;
            }
            case IDCANCEL:
            {
               EndDialog(hDlg, FALSE);

               return TRUE;
            }
            case IDC_UPLOADMEM:
            {
               if (HIWORD(wParam) == BN_CLICKED)
               {
                  EnableWindow(GetDlgItem(hDlg, IDC_EDITTEXT3), FALSE);
                  EnableWindow(GetDlgItem(hDlg, IDC_CHECKBOX1), TRUE);
               }

               break;
            }
            case IDC_DOWNLOADMEM:
            {
               if (HIWORD(wParam) == BN_CLICKED)
               {
                  EnableWindow(GetDlgItem(hDlg, IDC_EDITTEXT3), TRUE);
                  EnableWindow(GetDlgItem(hDlg, IDC_CHECKBOX1), FALSE);
               }
               break;
            }
            default: break;
         }
         break;
      }

      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

void DebugMouseWheel(HWND hctl, WPARAM wParam)
{
   if (HIWORD(wParam) < 0x8000)
      PostMessage(hctl, WM_VSCROLL, MAKEWPARAM(SB_LINEUP, 0), (LPARAM)NULL);
   else if (HIWORD(wParam) >= 0x8000)
      PostMessage(hctl, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), (LPARAM)NULL);
}

//////////////////////////////////////////////////////////////////////////////

void SH2UpdateRegList(HWND hDlg, sh2regs_struct *regs)
{
   char tempstr[128];
   int i;

   SendMessage(GetDlgItem(hDlg, IDC_REGLISTLB), LB_RESETCONTENT, 0, 0);

   for (i = 0; i < 16; i++)
   {                                       
      sprintf(tempstr, "R%02d =  %08x", i, (int)regs->R[i]);
      strupr(tempstr);
      SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
   }

   // SR
   sprintf(tempstr, "SR =   %08x", (int)regs->SR.all);
   strupr(tempstr);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   // GBR
   sprintf(tempstr, "GBR =  %08x", (int)regs->GBR);
   strupr(tempstr);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   // VBR
   sprintf(tempstr, "VBR =  %08x", (int)regs->VBR);
   strupr(tempstr);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   // MACH
   sprintf(tempstr, "MACH = %08x", (int)regs->MACH);
   strupr(tempstr);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   // MACL
   sprintf(tempstr, "MACL = %08x", (int)regs->MACL);
   strupr(tempstr);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   // PR
   sprintf(tempstr, "PR =   %08x", (int)regs->PR);
   strupr(tempstr);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   // PC
   sprintf(tempstr, "PC =   %08x", (int)regs->PC);
   strupr(tempstr);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
}

//////////////////////////////////////////////////////////////////////////////

void SH2UpdateCodeList(HWND hDlg, u32 addr)
{
   SendDlgItemMessage(hDlg, IDC_DISASM, DIS_GOTOADDRESS,0, addr-(11 * sizeof(u16)));
   SendDlgItemMessage(hDlg, IDC_DISASM, DIS_SETPC,0, addr);
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK MemDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                 LPARAM lParam)
{
   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         char buf[9];

         sprintf(buf, "%08lX", memaddr);
         SetDlgItemText(hDlg, IDC_EDITTEXT1, _16(buf));
         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (wParam)
         {
            case IDOK:
            {
               WCHAR buf[9];

               EndDialog(hDlg, TRUE);
               GetDlgItemText(hDlg, IDC_EDITTEXT1, buf, 11);

               wscanf(buf, "%08lx", &memaddr);

               return TRUE;
            }
            case IDCANCEL:
            {
               EndDialog(hDlg, FALSE);
               return TRUE;
            }
            default: break;
         }
         break;
      }
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

void SH2BreakpointHandler (SH2_struct *context, u32 addr)
{
   ScspMuteAudio();
   MessageBox (YabWin, _16("Breakpoint Reached"), _16("Notice"),  MB_OK | MB_ICONINFORMATION);

   debugsh = context;
   DialogBox(y_hInstance, MAKEINTRESOURCE(IDD_SH2DEBUG), YabWin, (DLGPROC)SH2DebugDlgProc);
   ScspUnMuteAudio();
}

//////////////////////////////////////////////////////////////////////////////

int SH2Dis(u32 addr, char *string)
{
   SH2Disasm(addr, MappedMemoryReadWord(addr), 0, string);
   return 2;
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK SH2DebugDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                 LPARAM lParam)
{
   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         sh2regs_struct sh2regs;
         codebreakpoint_struct *cbp;
         memorybreakpoint_struct *mbp;
         char tempstr[10];
         int i;

         SendDlgItemMessageA(hDlg, IDC_CODEBPET, EM_SETLIMITTEXT, 8, 0);
         SendDlgItemMessageA(hDlg, IDC_MEMBPET, EM_SETLIMITTEXT, 8, 0);

         cbp = SH2GetBreakpointList(debugsh);
         mbp = SH2GetMemoryBreakpointList(debugsh);

         for (i = 0; i < MAX_BREAKPOINTS; i++)
         {
            if (cbp[i].addr != 0xFFFFFFFF)
            {
               sprintf(tempstr, "%08X", (int)cbp[i].addr);
               SendMessageA(GetDlgItem(hDlg, IDC_CODEBPLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
            }

            if (mbp[i].addr != 0xFFFFFFFF)
            {
               sprintf(tempstr, "%08X", (int)mbp[i].addr);
               SendMessageA(GetDlgItem(hDlg, IDC_MEMBPLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
            }
         }

         SendDlgItemMessage(hDlg, IDC_DISASM, DIS_SETDISFUNC, 0, (LPARAM)SH2Dis);
         SendDlgItemMessage(hDlg, IDC_DISASM, DIS_SETENDADDRESS, 0, 0x06100000);

//         if (proc->paused())
//         {
            SH2GetRegisters(debugsh, &sh2regs);
            SH2UpdateRegList(hDlg, &sh2regs);
            SH2UpdateCodeList(hDlg, sh2regs.PC);
//         }


         SH2SetBreakpointCallBack(debugsh, (void (*)(void *, u32))&SH2BreakpointHandler);
         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDOK:
            {
               EndDialog(hDlg, TRUE);

               return TRUE;
            }
            case IDC_STEP:
            {
               sh2regs_struct sh2regs;
               SH2Step(debugsh);
               SH2GetRegisters(debugsh, &sh2regs);
               SH2UpdateRegList(hDlg, &sh2regs);
               SH2UpdateCodeList(hDlg, sh2regs.PC);

               break;
            }
            case IDC_STEPOVER:
            {
               break;
            }
            case IDC_MEMTRANSFER:
            {
               DialogBox(y_hInstance, MAKEINTRESOURCE(IDD_MEMTRANSFER), hDlg, (DLGPROC)MemTransferDlgProc);
               break;
            }
            case IDC_MEMEDITOR:
            {
               DialogBox(y_hInstance, MAKEINTRESOURCE(IDD_MEMORYEDITOR), hDlg, (DLGPROC)MemoryEditorDlgProc);
               break;
            }
            case IDC_ADDCODEBP:
            {
               char bptext[10];
               u32 addr=0;
               extern SH2Interface_struct *SH2Core;

               if (SH2Core->id != SH2CORE_DEBUGINTERPRETER)
               {
                  MessageBox (hDlg, _16("Breakpoints only supported by SH2 Debug Interpreter"), _16("Error"),  MB_OK | MB_ICONINFORMATION);
                  break;
               }
                  
               memset(bptext, 0, 10);
               GetDlgItemTextA(hDlg, IDC_CODEBPET, bptext, 10);

               if (bptext[0] != 0)
               {
                  sscanf(bptext, "%lX", &addr);
                  sprintf(bptext, "%08lX", addr);

                  if (SH2AddCodeBreakpoint(debugsh, addr) == 0)
                     SendMessageA(GetDlgItem(hDlg, IDC_CODEBPLB), LB_ADDSTRING, 0, (LPARAM)bptext);
               }
               break;
            }
            case IDC_DELCODEBP:
            {
               LRESULT ret;
               char bptext[10];
               u32 addr=0;
               extern SH2Interface_struct *SH2Core;

               if (SH2Core->id != SH2CORE_DEBUGINTERPRETER)
                  break;

               if ((ret = SendDlgItemMessage(hDlg, IDC_CODEBPLB, LB_GETCURSEL, 0, 0)) != LB_ERR)
               {
                  SendDlgItemMessageA(hDlg, IDC_CODEBPLB, LB_GETTEXT, ret, (LPARAM)bptext);
                  sscanf(bptext, "%lX", &addr);
                  SH2DelCodeBreakpoint(debugsh, addr);
                  SendDlgItemMessage(hDlg, IDC_CODEBPLB, LB_DELETESTRING, ret, 0);
               }
               break;
            }
            case IDC_ADDMEMBP:
            {
               char bptext[10];
               u32 addr=0;
               u32 flags=0;

               memset(bptext, 0, 10);
               GetDlgItemTextA(hDlg, IDC_MEMBPET, bptext, 10);

               if (bptext[0] != 0)
               {
                  sscanf(bptext, "%lX", &addr);
                  sprintf(bptext, "%08lX", addr);

                  if (SendDlgItemMessage(hDlg, IDC_CHKREAD, BM_GETCHECK, 0, 0) == BST_CHECKED)
                  {
                     if (SendDlgItemMessage(hDlg, IDC_CHKBYTE1, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        flags |= BREAK_BYTEREAD;
                     if (SendDlgItemMessage(hDlg, IDC_CHKWORD1, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        flags |= BREAK_WORDREAD;
                     if (SendDlgItemMessage(hDlg, IDC_CHKLONG1, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        flags |= BREAK_LONGREAD;
                  }

                  if (SendDlgItemMessage(hDlg, IDC_CHKWRITE, BM_GETCHECK, 0, 0) == BST_CHECKED)
                  {
                     if (SendDlgItemMessage(hDlg, IDC_CHKBYTE2, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        flags |= BREAK_BYTEWRITE;
                     if (SendDlgItemMessage(hDlg, IDC_CHKWORD2, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        flags |= BREAK_WORDWRITE;
                     if (SendDlgItemMessage(hDlg, IDC_CHKLONG2, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        flags |= BREAK_LONGWRITE;
                  }

                  if (SH2AddMemoryBreakpoint(debugsh, addr, flags) == 0)
                     SendMessageA(GetDlgItem(hDlg, IDC_MEMBPLB), LB_ADDSTRING, 0, (LPARAM)bptext);
               }
               break;
            }
            case IDC_DELMEMBP:
            {
               LRESULT ret;
               char bptext[10];
               u32 addr=0;

               if ((ret = SendDlgItemMessage(hDlg, IDC_MEMBPLB, LB_GETCURSEL, 0, 0)) != LB_ERR)
               {
                  SendDlgItemMessageA(hDlg, IDC_MEMBPLB, LB_GETTEXT, ret, (LPARAM)bptext);
                  sscanf(bptext, "%lX", &addr);
                  SH2DelMemoryBreakpoint(debugsh, addr);
                  SendDlgItemMessage(hDlg, IDC_MEMBPLB, LB_DELETESTRING, ret, 0);
               }

               break;
            }
            case IDC_CHKREAD:
            {
               LRESULT ret;

               if (HIWORD(wParam) == BN_CLICKED)
               {
                  if ((ret = SendDlgItemMessage(hDlg, IDC_CHKREAD, BM_GETCHECK, 0, 0)) == BST_UNCHECKED)
                  {
                     SendDlgItemMessage(hDlg, IDC_CHKREAD, BM_SETCHECK, BST_CHECKED, 0);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKBYTE1), TRUE);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKWORD1), TRUE);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKLONG1), TRUE);
                  }
                  else if (ret == BST_CHECKED)
                  {
                     SendDlgItemMessage(hDlg, IDC_CHKREAD, BM_SETCHECK, BST_UNCHECKED, 0);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKBYTE1), FALSE);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKWORD1), FALSE);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKLONG1), FALSE);
                  }
               }

               break;
            }
            case IDC_CHKWRITE:
            {
               LRESULT ret;

               if (HIWORD(wParam) == BN_CLICKED)
               {
                  if ((ret = SendDlgItemMessage(hDlg, IDC_CHKWRITE, BM_GETCHECK, 0, 0)) == BST_UNCHECKED)
                  {
                     SendDlgItemMessage(hDlg, IDC_CHKWRITE, BM_SETCHECK, BST_CHECKED, 0);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKBYTE2), TRUE);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKWORD2), TRUE);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKLONG2), TRUE);
                  }
                  else if (ret == BST_CHECKED)
                  {
                     SendDlgItemMessage(hDlg, IDC_CHKWRITE, BM_SETCHECK, BST_UNCHECKED, 0);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKBYTE2), FALSE);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKWORD2), FALSE);
                     EnableWindow(GetDlgItem(hDlg, IDC_CHKLONG2), FALSE);
                  }
               }

               break;
            }
            case IDC_REGLISTLB:
            {
               switch (HIWORD(wParam))
               {
                  case LBN_DBLCLK:
                  {
                     // dialogue for changing register values
                     int cursel;

                     sh2regs_struct sh2regs;
                     SH2GetRegisters(debugsh, &sh2regs);
                     cursel = (int)SendMessage(GetDlgItem(hDlg,LOWORD(wParam)), LB_GETCURSEL,0,0);

                     if (cursel < 16)
                     {
                        memaddr = sh2regs.R[cursel];
                     }
                     else if (cursel == 16)
                     {
                        memaddr = sh2regs.SR.all;
                     }
                     else if (cursel == 17)
                     {
                        memaddr = sh2regs.GBR;
                     }
                     else if (cursel == 18)
                     {
                        memaddr = sh2regs.VBR;
                     }
                     else if (cursel == 19)
                     {
                        memaddr = sh2regs.MACH;
                     }
                     else if (cursel == 20)
                     {
                        memaddr = sh2regs.MACL;
                     }
                     else if (cursel == 21)
                     {
                        memaddr = sh2regs.PR;
                     }
                     else if (cursel == 22)
                     {
                        memaddr = sh2regs.PC;
                     }

                     if (DialogBox(GetModuleHandle(0), MAKEINTRESOURCE(IDD_MEM), hDlg, (DLGPROC)MemDlgProc) != FALSE)
                     {
                        if (cursel < 16)
                        {
                           sh2regs.R[cursel] = memaddr;
                        }
                        else if (cursel == 16)
                        {
                           sh2regs.SR.all = memaddr;
                        }
                        else if (cursel == 17)
                        {
                           sh2regs.GBR = memaddr;
                        }
                        else if (cursel == 18)
                        {
                           sh2regs.VBR = memaddr;
                        }
                        else if (cursel == 19)
                        {
                           sh2regs.MACH = memaddr;
                        }
                        else if (cursel == 20)
                        {
                           sh2regs.MACL = memaddr;
                        }
                        else if (cursel == 21)
                        {
                           sh2regs.PR = memaddr;
                        }
                        else if (cursel == 22)
                        {
                           sh2regs.PC = memaddr;
                           SH2UpdateCodeList(hDlg, sh2regs.PC);
                        }
                     }

                     SH2SetRegisters(debugsh, &sh2regs);
                     SH2UpdateRegList(hDlg, &sh2regs);
                     break;
                  }
                  default: break;
               }

               break;
            }
            case IDC_DISASM:
            {
               switch (HIWORD(wParam))
               {
                  case LBN_DBLCLK:
                  {
                     // Add a code breakpoint when code is double-clicked
                     char bptext[10];
                     u32 addr=0;
                     extern SH2Interface_struct *SH2Core;

                     if (SH2Core->id != SH2CORE_DEBUGINTERPRETER)
                     {
                        MessageBox (hDlg, _16("Breakpoints only supported by SH2 Debug Interpreter"), _16("Error"),  MB_OK | MB_ICONINFORMATION);
                        break;
                     }

                     addr = (u32)SendMessage(GetDlgItem(hDlg,LOWORD(wParam)), DIS_GETCURSEL,0,0);
                     sprintf(bptext, "%08X", (int)addr);

                     if (SH2AddCodeBreakpoint(debugsh, addr) == 0)
                        SendMessage(GetDlgItem(hDlg, IDC_CODEBPLB), LB_ADDSTRING, 0, (LPARAM)bptext);
                     break;
                  }
                  default: break;
               }

               break;
            }

            default: break;
         }
         break;
      }
      case WM_MOUSEWHEEL:
         DebugMouseWheel(GetDlgItem(hDlg, IDC_DISASM), wParam);
         return TRUE;
      case WM_CLOSE:
      {
         EndDialog(hDlg, TRUE);

         return TRUE;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

int SaveBitmap(const char *filename, int width, int height, u32 *data)
{
   BITMAPFILEHEADER fileheader;
   BITMAPV4HEADER bmi;
   FILE *fp;
   int i;

   if (!filename)
      return -1;

   if ((fp = fopen(filename, "wb")) == NULL)
      return -1;

   memset(&fileheader, 0, sizeof(fileheader));
   fileheader.bfType = 'B' | ('M' << 8);
   fileheader.bfSize = sizeof(fileheader) + sizeof(bmi) + (width * height * 4);
   fileheader.bfOffBits = sizeof(fileheader)+sizeof(bmi);
   fwrite((void *)&fileheader, 1, sizeof(fileheader), fp);

   memset(&bmi, 0, sizeof(bmi));
   bmi.bV4Size = sizeof(bmi);
   bmi.bV4Planes = 1;
   bmi.bV4BitCount = 32;
   bmi.bV4V4Compression = BI_RGB | BI_BITFIELDS;
   bmi.bV4RedMask = 0x000000FF;
   bmi.bV4GreenMask = 0x0000FF00;
   bmi.bV4BlueMask = 0x00FF0000;
   bmi.bV4AlphaMask = 0xFF000000;
   bmi.bV4Width = width;
   bmi.bV4Height = -height;
   fwrite((void *)&bmi, 1, sizeof(bmi), fp);

   for (i = 0; i < height; i++)
   {
      fwrite(data, 1, width * sizeof(u32), fp);
      data += width;
   }
   fclose(fp);

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK VDP1DebugDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                 LPARAM lParam)
{
   char tempstr[1024];
   char filename[MAX_PATH] = "\0";

   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         char *string;
         u32 i=0;

         // execute yabause until vblank-out
         if (YabauseExec() != 0)
            return FALSE;

         // Build command list
         SendMessage(GetDlgItem(hDlg, IDC_VDP1CMDLB), LB_RESETCONTENT, 0, 0);

         for (;;)
         {
            if ((string = Vdp1DebugGetCommandNumberName(i)) == NULL)
               break;

            SendMessage(GetDlgItem(hDlg, IDC_VDP1CMDLB), LB_ADDSTRING, 0, (LPARAM) _16(string));

            i++;
         }

         vdp1texturew = vdp1textureh = 1;
         EnableWindow(GetDlgItem(hDlg, IDC_VDP1SAVEBMPBT), vdp1texture ? TRUE : FALSE);

         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDC_VDP1CMDLB:
            {
               switch(HIWORD(wParam))
               {
                  case LBN_SELCHANGE:
                  {
                     u32 cursel=0;

                     cursel = (u32)SendDlgItemMessage(hDlg, IDC_VDP1CMDLB, LB_GETCURSEL, 0, 0);

                     Vdp1DebugCommand(cursel, tempstr);
                     SetDlgItemText(hDlg, IDC_VDP1CMDET, _16(tempstr));

                     if (vdp1texture)
                        free(vdp1texture);

                     vdp1texture = Vdp1DebugTexture(cursel, &vdp1texturew, &vdp1textureh);
                     EnableWindow(GetDlgItem(hDlg, IDC_VDP1SAVEBMPBT), vdp1texture ? TRUE : FALSE);
                     InvalidateRect(hDlg, NULL, FALSE);
                     UpdateWindow(hDlg);

                     return TRUE;
                  }
                  default: break;
               }
               return TRUE;
            }
            case IDC_VDP1SAVEBMPBT:
            {
               OPENFILENAME ofn;
               WCHAR filter[1024];

               CreateFilter(filter, 1024,
                  "Bitmap Files", "*.BMP",
                  "All files (*.*)", "*.*", NULL);

               SetupOFN(&ofn, OFN_DEFAULTSAVE, hDlg, filter, filename, sizeof(filename));
               ofn.lpstrDefExt = _16("BMP");

               if (vdp1texture && GetSaveFileName(&ofn))
                  SaveBitmap(filename, vdp1texturew, vdp1textureh, vdp1texture);

               return TRUE;
            }
            case IDOK:
            {
               EndDialog(hDlg, TRUE);

               return TRUE;
            }

            default: break;
         }
         break;
      }
      case WM_PAINT:
      {
         // Draw our texture box
         PAINTSTRUCT ps;
         HDC hdc;
         BITMAPV4HEADER bmi;
         int outw, outh;
         RECT rect;

         hdc = BeginPaint(GetDlgItem(hDlg, IDC_VDP1TEXTET), &ps);
         GetClientRect(GetDlgItem(hDlg, IDC_VDP1TEXTET), &rect);
         FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));

         if (vdp1texture == NULL || vdp1texturew == 0 || vdp1textureh == 0)
         {
            SetBkColor(hdc, RGB(0,0,0));
            SetTextColor(hdc, RGB(255,255,255));
            TextOut(hdc, 0, 0, _16("Not Available"), 13);
         }
         else
         {
            memset(&bmi, 0, sizeof(bmi));
            bmi.bV4Size = sizeof(bmi);
            bmi.bV4Planes = 1;
            bmi.bV4BitCount = 32;
            bmi.bV4V4Compression = BI_RGB | BI_BITFIELDS; // double-check this
            bmi.bV4RedMask = 0x000000FF;
            bmi.bV4GreenMask = 0x0000FF00;
            bmi.bV4BlueMask = 0x00FF0000;
            bmi.bV4AlphaMask = 0xFF000000;
            bmi.bV4Width = vdp1texturew;
            bmi.bV4Height = -vdp1textureh;

            // Let's try to maintain a correct ratio
            if (vdp1texturew > vdp1textureh)
            {
               outw = rect.right;
               outh = rect.bottom * vdp1textureh / vdp1texturew;
            }
            else
            {
               outw = rect.right * vdp1texturew / vdp1textureh;
               outh = rect.bottom;
            }
   
            StretchDIBits(hdc, 0, 0, outw, outh, 0, 0, vdp1texturew, vdp1textureh, vdp1texture, (BITMAPINFO *)&bmi, DIB_RGB_COLORS, SRCCOPY);
         }
         EndPaint(GetDlgItem(hDlg, IDC_VDP1TEXTET), &ps);
         break;
      }
      case WM_CLOSE:
      {
         EndDialog(hDlg, TRUE);

         return TRUE;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK VDP2ViewerDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                 LPARAM lParam)
{
   static u32 *vdp2texture;
   static int width;
   static int height;
   char filename[MAX_PATH] = "\0";

   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         vdp2texture = NULL;

         SendDlgItemMessage(hDlg, IDC_VDP2SCREENCB, CB_RESETCONTENT, 0, 0);
         SendDlgItemMessage(hDlg, IDC_VDP2SCREENCB, CB_ADDSTRING, 0, (LPARAM)_16("NBG0/RBG1"));
         SendDlgItemMessage(hDlg, IDC_VDP2SCREENCB, CB_ADDSTRING, 0, (LPARAM)_16("NBG1"));
         SendDlgItemMessage(hDlg, IDC_VDP2SCREENCB, CB_ADDSTRING, 0, (LPARAM)_16("NBG2"));
         SendDlgItemMessage(hDlg, IDC_VDP2SCREENCB, CB_ADDSTRING, 0, (LPARAM)_16("NBG3"));
         SendDlgItemMessage(hDlg, IDC_VDP2SCREENCB, CB_ADDSTRING, 0, (LPARAM)_16("RBG0"));
         SendDlgItemMessage(hDlg, IDC_VDP2SCREENCB, CB_SETCURSEL, 0, 0);

         vdp2texture = Vdp2DebugTexture(0, -1, 0x00FF00FF, &width, &height);
         EnableWindow(GetDlgItem(hDlg, IDC_VDP2SAVEBMPBT), vdp2texture ? TRUE : FALSE);
         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDC_VDP2SCREENCB:
            {
               switch(HIWORD(wParam))
               {
                  case CBN_SELCHANGE:
                  {
                     u8 cursel = (u8)SendDlgItemMessage(hDlg, IDC_VDP2SCREENCB, CB_GETCURSEL, 0, 0);

                     if (vdp2texture)
                        free(vdp2texture);

                     vdp2texture = Vdp2DebugTexture(cursel, -1, 0x00FF00FF, &width, &height);
                     EnableWindow(GetDlgItem(hDlg, IDC_VDP2SAVEBMPBT), vdp2texture ? TRUE : FALSE);

                     InvalidateRect(hDlg, NULL, FALSE);
                     UpdateWindow(hDlg);
                     return TRUE;
                  }
                  default: break;
               }

               return TRUE;
            }
            case IDC_VDP2SAVEBMPBT:
            {
               OPENFILENAME ofn;
               WCHAR filter[1024];

               CreateFilter(filter, 1024,
                  "Bitmap Files", "*.BMP",
                  "All files (*.*)", "*.*", NULL);

               SetupOFN(&ofn, OFN_DEFAULTSAVE, hDlg, filter, filename, sizeof(filename));
               ofn.lpstrDefExt = _16("BMP");

               if (GetSaveFileName(&ofn))
                  SaveBitmap(filename, width, height, vdp2texture);

               return TRUE;
            }
            case IDOK:
               EndDialog(hDlg, TRUE);
               return TRUE;
            default: break;
         }
         break;
      }
      case WM_PAINT:
      {
         // Draw our texture box
         PAINTSTRUCT ps;
         HDC hdc;
         BITMAPV4HEADER bmi;
         int outw, outh;
         RECT rect;

         hdc = BeginPaint(GetDlgItem(hDlg, IDC_VDP2TEXTET), &ps);
         GetClientRect(GetDlgItem(hDlg, IDC_VDP2TEXTET), &rect);
         FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));

         if (vdp2texture == NULL)
         {
            SetBkColor(hdc, RGB(0,0,0));
            SetTextColor(hdc, RGB(255,255,255));
            TextOut(hdc, 0, 0, _16("Not Available"), 13);
         }
         else
         {
            memset(&bmi, 0, sizeof(bmi));
            bmi.bV4Size = sizeof(bmi);
            bmi.bV4Planes = 1;
            bmi.bV4BitCount = 32;
            bmi.bV4V4Compression = BI_RGB | BI_BITFIELDS;
            bmi.bV4RedMask = 0x000000FF;
            bmi.bV4GreenMask = 0x0000FF00;
            bmi.bV4BlueMask = 0x00FF0000;
            bmi.bV4AlphaMask = 0xFF000000;
            bmi.bV4Width = width;
            bmi.bV4Height = -height;

            // Let's try to maintain a correct ratio
            if (width > height)
            {
               outw = rect.right;
               outh = rect.bottom * height / width;
            }
            else
            {
               outw = rect.right * width / height;
               outh = rect.bottom;
            }
   
            StretchDIBits(hdc, 0, 0, outw, outh, 0, 0, width, height, vdp2texture, (BITMAPINFO *)&bmi, DIB_RGB_COLORS, SRCCOPY);
         }
         EndPaint(GetDlgItem(hDlg, IDC_VDP2TEXTET), &ps);
         break;
      }
      case WM_CLOSE:
         EndDialog(hDlg, TRUE);

         return TRUE;
      case WM_DESTROY:
      {
         if (vdp2texture)
            free(vdp2texture);
         return TRUE;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK VDP2DebugDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                 LPARAM lParam)
{
   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         char tempstr[VDP2_DEBUG_STRING_SIZE];
         int isscrenabled;

         // is NBG0/RBG1 enabled?
         Vdp2DebugStatsNBG0(tempstr, &isscrenabled);

         if (isscrenabled)
         {
            SendMessage(GetDlgItem(hDlg, IDC_NBG0ENABCB), BM_SETCHECK, BST_CHECKED, 0);
            SetDlgItemText(hDlg, IDC_NBG0ET, _16(tempstr));
         }
         else
            SendMessage(GetDlgItem(hDlg, IDC_NBG0ENABCB), BM_SETCHECK, BST_UNCHECKED, 0);

         Vdp2DebugStatsNBG1(tempstr, &isscrenabled);

         // is NBG1 enabled?
         if (isscrenabled)
         {
            // enabled
            SendMessage(GetDlgItem(hDlg, IDC_NBG1ENABCB), BM_SETCHECK, BST_CHECKED, 0);
            SetDlgItemText(hDlg, IDC_NBG1ET, _16(tempstr));
         }
         else
            // disabled
            SendMessage(GetDlgItem(hDlg, IDC_NBG1ENABCB), BM_SETCHECK, BST_UNCHECKED, 0);

         Vdp2DebugStatsNBG2(tempstr, &isscrenabled);

         // is NBG2 enabled?
         if (isscrenabled)
         {
            // enabled
            SendMessage(GetDlgItem(hDlg, IDC_NBG2ENABCB), BM_SETCHECK, BST_CHECKED, 0);
            SetDlgItemText(hDlg, IDC_NBG2ET, _16(tempstr));
         }
         else
            // disabled
            SendMessage(GetDlgItem(hDlg, IDC_NBG2ENABCB), BM_SETCHECK, BST_UNCHECKED, 0);

         Vdp2DebugStatsNBG3(tempstr, &isscrenabled);

         // is NBG3 enabled?
         if (isscrenabled)
         {
            // enabled
            SendMessage(GetDlgItem(hDlg, IDC_NBG3ENABCB), BM_SETCHECK, BST_CHECKED, 0);
            SetDlgItemText(hDlg, IDC_NBG3ET, _16(tempstr));
         }
         else
            // disabled
            SendMessage(GetDlgItem(hDlg, IDC_NBG3ENABCB), BM_SETCHECK, BST_UNCHECKED, 0);

         Vdp2DebugStatsRBG0(tempstr, &isscrenabled);

         // is RBG0 enabled?
         if (isscrenabled)
         {
            // enabled
            SendMessage(GetDlgItem(hDlg, IDC_RBG0ENABCB), BM_SETCHECK, BST_CHECKED, 0);
            SetDlgItemText(hDlg, IDC_RBG0ET, _16(tempstr));
         }
         else
            // disabled
            SendMessage(GetDlgItem(hDlg, IDC_RBG0ENABCB), BM_SETCHECK, BST_UNCHECKED, 0);

         Vdp2DebugStatsGeneral(tempstr, &isscrenabled);

         if (isscrenabled)
         {
            // enabled
            SendMessage(GetDlgItem(hDlg, IDC_DISPENABCB), BM_SETCHECK, BST_CHECKED, 0);
            SetDlgItemText(hDlg, IDC_VDP2GENET, _16(tempstr));
         }
         else
            // disabled
            SendMessage(GetDlgItem(hDlg, IDC_DISPENABCB), BM_SETCHECK, BST_UNCHECKED, 0);


         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDOK:
               EndDialog(hDlg, TRUE);
               return TRUE;
            case IDC_VDP2VIEWER:
            {
               DialogBox(y_hInstance, MAKEINTRESOURCE(IDD_VDP2VIEWER), NULL, (DLGPROC)VDP2ViewerDlgProc);
               return TRUE;
            }
            default: break;
         }
         break;
      }
      case WM_CLOSE:
      {
         EndDialog(hDlg, TRUE);

         return TRUE;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

void M68KUpdateRegList(HWND hDlg, m68kregs_struct *regs)
{
   char tempstr[128];
   int i;

   SendMessage(GetDlgItem(hDlg, IDC_REGLISTLB), LB_RESETCONTENT, 0, 0);

   // Data registers
   for (i = 0; i < 8; i++)
   {
      sprintf(tempstr, "D%d =   %08x", i, (int)regs->D[i]);
      strupr(tempstr);
      SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
   }

   // Address registers
   for (i = 0; i < 8; i++)
   {
      sprintf(tempstr, "A%d =   %08x", i, (int)regs->A[i]);
      strupr(tempstr);
      SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
   }

   // SR
   sprintf(tempstr, "SR =   %08x", (int)regs->SR);
   strupr(tempstr);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   // PC
   sprintf(tempstr, "PC =   %08x", (int)regs->PC);
   strupr(tempstr);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
}

//////////////////////////////////////////////////////////////////////////////

void M68KBreakpointHandler (u32 addr)
{
   ScspMuteAudio();
   MessageBox (YabWin, _16("Breakpoint Reached"), _16("Notice"),  MB_OK | MB_ICONINFORMATION);
   DialogBox(y_hInstance, MAKEINTRESOURCE(IDD_M68KDEBUG), YabWin, (DLGPROC)M68KDebugDlgProc);
   ScspUnMuteAudio();
}

//////////////////////////////////////////////////////////////////////////////

void M68KUpdateCodeList(HWND hDlg, u32 addr)
{
   SendDlgItemMessage(hDlg, IDC_DISASM, DIS_GOTOADDRESS, 0, addr);
   SendDlgItemMessage(hDlg, IDC_DISASM, DIS_SETPC, 0, addr);
}

//////////////////////////////////////////////////////////////////////////////

int M68KDis(u32 addr, char *string)
{
   return (int)(M68KDisasm(addr, string) - addr);
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK M68KDebugDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                  LPARAM lParam)
{
   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         m68kregs_struct m68kregs;
         m68kcodebreakpoint_struct *cbp;
         char tempstr[10];
         int i;

         EnableWindow(GetDlgItem(hDlg, IDC_STEP), TRUE);

         SendDlgItemMessageA(hDlg, IDC_CODEBPET, EM_SETLIMITTEXT, 5, 0);
         cbp = M68KGetBreakpointList();

         for (i = 0; i < MAX_BREAKPOINTS; i++)
         {
            if (cbp[i].addr != 0xFFFFFFFF)
            {
               sprintf(tempstr, "%08X", (int)cbp[i].addr);
               SendMessageA(GetDlgItem(hDlg, IDC_CODEBPLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
            }
         }

         SendDlgItemMessage(hDlg, IDC_DISASM, DIS_SETDISFUNC, 0, (LPARAM)M68KDis);
         SendDlgItemMessage(hDlg, IDC_DISASM, DIS_SETENDADDRESS, 0, 0x100000);

         M68KGetRegisters(&m68kregs);
         M68KUpdateRegList(hDlg, &m68kregs);
         M68KUpdateCodeList(hDlg, m68kregs.PC);

         M68KSetBreakpointCallBack(&M68KBreakpointHandler);
         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDOK:
            {
               EndDialog(hDlg, TRUE);

               return TRUE;
            }
            case IDC_STEP:
            {
               m68kregs_struct m68kregs;

               // execute instruction
               M68KStep();

               M68KGetRegisters(&m68kregs);
               M68KUpdateRegList(hDlg, &m68kregs);
               M68KUpdateCodeList(hDlg, m68kregs.PC);
               break;
            }
            case IDC_ADDCODEBP:
            {
               // add a code breakpoint
               char bptext[10];
               u32 addr=0;
               memset(bptext, 0, 10);
               GetDlgItemTextA(hDlg, IDC_CODEBPET, bptext, 10);

               if (bptext[0] != 0)
               {
                  sscanf(bptext, "%lX", &addr);
                  sprintf(bptext, "%05X", (int)addr);

                  if (M68KAddCodeBreakpoint(addr) == 0)
                     SendMessageA(GetDlgItem(hDlg, IDC_CODEBPLB), LB_ADDSTRING, 0, (LPARAM)bptext);
               }
               break;
            }
            case IDC_DELCODEBP:
            {
               // delete a code breakpoint
               LRESULT ret;
               char bptext[10];
               u32 addr=0;

               if ((ret = SendDlgItemMessage(hDlg, IDC_CODEBPLB, LB_GETCURSEL, 0, 0)) != LB_ERR)
               {
                  SendDlgItemMessageA(hDlg, IDC_CODEBPLB, LB_GETTEXT, ret, (LPARAM)bptext);
                  sscanf(bptext, "%lX", &addr);
                  M68KDelCodeBreakpoint(addr);
                  SendDlgItemMessage(hDlg, IDC_CODEBPLB, LB_DELETESTRING, ret, 0);
               }

               break;
            }
            case IDC_REGLISTLB:
            {
               switch (HIWORD(wParam))
               {
                  case LBN_DBLCLK:
                  {
                     // dialogue for changing register values
                     int cursel;
                     m68kregs_struct m68kregs;

                     cursel = (int)SendMessage(GetDlgItem(hDlg,LOWORD(wParam)), LB_GETCURSEL,0,0);

                     M68KGetRegisters(&m68kregs);

                     switch (cursel)
                     {
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                        case 4:
                        case 5:
                        case 6:
                        case 7:
                           memaddr = m68kregs.D[cursel];                           
                           break;
                        case 8:
                        case 9:
                        case 10:
                        case 11:
                        case 12:
                        case 13:
                        case 14:
                        case 15:
                           memaddr = m68kregs.A[cursel - 8];
                           break;
                        case 16:
                           memaddr = m68kregs.SR;
                           break;
                        case 17:
                           memaddr = m68kregs.PC;
                           break;
                        default: break;
                     }

                     if (DialogBox(y_hInstance, MAKEINTRESOURCE(IDD_MEM), hDlg, (DLGPROC)MemDlgProc) == TRUE)
                     {
                        switch (cursel)
                        {
                           case 0:
                           case 1:
                           case 2:
                           case 3:
                           case 4:
                           case 5:
                           case 6:
                           case 7:
                              m68kregs.D[cursel] = memaddr;
                              break;
                           case 8:
                           case 9:
                           case 10:
                           case 11:
                           case 12:
                           case 13:
                           case 14:
                           case 15:
                              m68kregs.A[cursel - 8] = memaddr;
                              break;
                           case 16:
                              m68kregs.SR = memaddr;
                              break;
                           case 17:
                              m68kregs.PC = memaddr;
                              M68KUpdateCodeList(hDlg, m68kregs.PC);
                              break;
                           default: break;
                        }

                        M68KSetRegisters(&m68kregs);
                     }

                     M68KUpdateRegList(hDlg, &m68kregs);
                     break;
                  }
                  default: break;
               }

               break;
            }
            case IDC_DISASM:
            {
               switch (HIWORD(wParam))
               {
                  case LBN_DBLCLK:
                  {
                     // Add a code breakpoint when code is double-clicked
                     char bptext[10];
                     u32 addr=0;

                     addr = (u32)SendMessage(GetDlgItem(hDlg,LOWORD(wParam)), DIS_GETCURSEL,0,0);
                     sprintf(bptext, "%05X", (int)addr);

                     if (M68KAddCodeBreakpoint(addr) == 0)
                        SendMessage(GetDlgItem(hDlg, IDC_CODEBPLB), LB_ADDSTRING, 0, (LPARAM)bptext);
                     break;
                  }
                  default: break;
               }

               break;
            }
            default: break;
         }
         break;
      }
      case WM_MOUSEWHEEL:
         DebugMouseWheel(GetDlgItem(hDlg, IDC_DISASM), wParam);
         return TRUE;
      case WM_CLOSE:
      {
         EndDialog(hDlg, TRUE);

         return TRUE;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

void SCUDSPUpdateRegList(HWND hDlg, scudspregs_struct *regs)
{
   char tempstr[128];

   SendMessage(GetDlgItem(hDlg, IDC_REGLISTLB), LB_RESETCONTENT, 0, 0);

   sprintf(tempstr, "PR = %d   EP = %d", regs->ProgControlPort.part.PR, regs->ProgControlPort.part.EP);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "T0 = %d   S =  %d", regs->ProgControlPort.part.T0, regs->ProgControlPort.part.S);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "Z =  %d   C =  %d", regs->ProgControlPort.part.Z, regs->ProgControlPort.part.C);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "V =  %d   E =  %d", regs->ProgControlPort.part.V, regs->ProgControlPort.part.E);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "ES = %d   EX = %d", regs->ProgControlPort.part.ES, regs->ProgControlPort.part.EX);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "LE =          %d", regs->ProgControlPort.part.LE);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "P =          %02X", regs->ProgControlPort.part.P);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "TOP =        %02X", regs->TOP);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "LOP =        %02X", regs->LOP);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "CT = %02X:%02X:%02X:%02X", regs->CT[0], regs->CT[1], regs->CT[2], regs->CT[3]);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "RA =   %08lX", regs->RA0);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "WA =   %08lX", regs->WA0);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "RX =   %08lX", regs->RX);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "RY =   %08lX", regs->RX);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "PH =       %04X", regs->P.part.H & 0xFFFF);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "PL =   %08X", (int)(regs->P.part.L & 0xFFFFFFFF));
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "ACH =      %04X", regs->AC.part.H & 0xFFFF);
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);

   sprintf(tempstr, "ACL =  %08X", (int)(regs->AC.part.L & 0xFFFFFFFF));
   SendMessageA(GetDlgItem(hDlg, IDC_REGLISTLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
}

//////////////////////////////////////////////////////////////////////////////

void SCUDSPUpdateCodeList(HWND hDlg, u8 addr)
{
   if (addr < 11)
      SendDlgItemMessage(hDlg, IDC_DISASM, DIS_GOTOADDRESS,0, 0);
   else
      SendDlgItemMessage(hDlg, IDC_DISASM, DIS_GOTOADDRESS,0, addr-11);
   SendDlgItemMessage(hDlg, IDC_DISASM, DIS_SETPC,0, addr);
}

//////////////////////////////////////////////////////////////////////////////

void SCUDSPBreakpointHandler (u32 addr)
{
   ScspMuteAudio();
   MessageBox (YabWin, _16("Breakpoint Reached"), _16("Notice"),  MB_OK | MB_ICONINFORMATION);
   DialogBox(y_hInstance, MAKEINTRESOURCE(IDD_SCUDSPDEBUG), YabWin, (DLGPROC)SCUDSPDebugDlgProc);
   ScspUnMuteAudio();
}

//////////////////////////////////////////////////////////////////////////////
int SCUDSPDis(u32 addr, char *string)
{
   ScuDspDisasm((u8)addr, string);
   return 1;
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK SCUDSPDebugDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                  LPARAM lParam)
{
   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         scudspregs_struct dspregs;
         scucodebreakpoint_struct *cbp;
         char tempstr[10];
         int i;

         SendDlgItemMessageA(hDlg, IDC_CODEBPET, EM_SETLIMITTEXT, 2, 0);
         cbp = ScuDspGetBreakpointList();

         for (i = 0; i < MAX_BREAKPOINTS; i++)
         {
            if (cbp[i].addr != 0xFFFFFFFF)
            {
               sprintf(tempstr, "%02X", (int)cbp[i].addr);
               SendMessageA(GetDlgItem(hDlg, IDC_CODEBPLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
            }
         }

         SendDlgItemMessage(hDlg, IDC_DISASM, DIS_SETDISFUNC, 0, (LPARAM)SCUDSPDis);
         SendDlgItemMessage(hDlg, IDC_DISASM, DIS_SETENDADDRESS, 0, 0x100);

         EnableWindow(GetDlgItem(hDlg, IDC_STEP), TRUE);

         ScuDspGetRegisters(&dspregs);
         SCUDSPUpdateRegList(hDlg, &dspregs);
         SCUDSPUpdateCodeList(hDlg, (u8)dspregs.ProgControlPort.part.P);

         ScuDspSetBreakpointCallBack(&SCUDSPBreakpointHandler);

         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDOK:
            {
               EndDialog(hDlg, TRUE);

               return TRUE;
            }
            case IDC_STEP:
            {
               scudspregs_struct dspregs;

               ScuDspStep();

               ScuDspGetRegisters(&dspregs);
               SCUDSPUpdateRegList(hDlg, &dspregs);
               SCUDSPUpdateCodeList(hDlg, (u8)dspregs.ProgControlPort.part.P);

               break;
            }
            case IDC_ADDCODEBP:
            {
               // add a code breakpoint
               char bptext[10];
               u32 addr=0;
               memset(bptext, 0, 4);
               GetDlgItemTextA(hDlg, IDC_CODEBPET, bptext, 4);

               if (bptext[0] != 0)
               {
                  sscanf(bptext, "%lX", &addr);
                  sprintf(bptext, "%02X", (int)addr);

                  if (ScuDspAddCodeBreakpoint(addr) == 0)
                     SendMessageA(GetDlgItem(hDlg, IDC_CODEBPLB), LB_ADDSTRING, 0, (LPARAM)bptext);
               }
               break;
            }
            case IDC_DELCODEBP:
            {
               // delete a code breakpoint
               LRESULT ret;
               char bptext[10];
               u32 addr=0;

               if ((ret = SendDlgItemMessage(hDlg, IDC_CODEBPLB, LB_GETCURSEL, 0, 0)) != LB_ERR)
               {
                  SendDlgItemMessageA(hDlg, IDC_CODEBPLB, LB_GETTEXT, ret, (LPARAM)bptext);
                  sscanf(bptext, "%lX", &addr);
                  ScuDspDelCodeBreakpoint(addr);
                  SendDlgItemMessage(hDlg, IDC_CODEBPLB, LB_DELETESTRING, ret, 0);
               }

               break;
            }
            case IDC_DISASM:
            {
               switch (HIWORD(wParam))
               {
                  case LBN_DBLCLK:
                  {
                     // Add a code breakpoint when code is double-clicked
                     char bptext[10];
                     u32 addr=0;

                     addr = (u32)SendMessage(GetDlgItem(hDlg,LOWORD(wParam)), DIS_GETCURSEL,0,0);
                     sprintf(bptext, "%02X", (int)addr);

                     if (ScuDspAddCodeBreakpoint(addr) == 0)
                        SendMessage(GetDlgItem(hDlg, IDC_CODEBPLB), LB_ADDSTRING, 0, (LPARAM)bptext);

                     break;
                  }
                  default: break;
               }

               break;
            }
            default: break;
         }
         break;
      }
      case WM_MOUSEWHEEL:
         DebugMouseWheel(GetDlgItem(hDlg, IDC_DISASM), wParam);
         return TRUE;
      case WM_CLOSE:
      {
         EndDialog(hDlg, TRUE);

         return TRUE;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK SCSPDebugDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                  LPARAM lParam)
{
   char tempstr[2048];
   int i;

   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         SendDlgItemMessage(hDlg, IDC_SCSPSLOTCB, CB_RESETCONTENT, 0, 0);

         for (i = 0; i < 32; i++)
         {
            sprintf(tempstr, "%d", i);
            SendDlgItemMessage(hDlg, IDC_SCSPSLOTCB, CB_ADDSTRING, 0, (LPARAM) _16(tempstr));
         }

         SendDlgItemMessage(hDlg, IDC_SCSPSLOTCB, CB_SETCURSEL, 0, 0);

         // Setup Slot Info
         ScspSlotDebugStats(0, tempstr);
         SetDlgItemText(hDlg, IDC_SCSPSLOTET, _16(tempstr));

         // Setup Common Control registers
         ScspCommonControlRegisterDebugStats(tempstr);
         SetDlgItemText(hDlg, IDC_SCSPCOMMONREGET, _16(tempstr));

         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDC_SCSPSLOTCB:
            {
               switch(HIWORD(wParam))
               {
                  case CBN_SELCHANGE:
                  {
                     u8 cursel=0;

                     // Update Sound Slot Info
                     cursel = (u8)SendDlgItemMessage(hDlg, IDC_SCSPSLOTCB, CB_GETCURSEL, 0, 0);

                     ScspSlotDebugStats(cursel, tempstr);
                     SetDlgItemText(hDlg, IDC_SCSPSLOTET, _16(tempstr));

                     return TRUE;
                  }
                  default: break;
               }

               return TRUE;
            }
            case IDC_SCSPSLOTSAVE:
            {
               OPENFILENAME ofn;
               u8 cursel=0;
               WCHAR filter[1024];

               cursel = (u8)SendDlgItemMessage(hDlg, IDC_SCSPSLOTCB, CB_GETCURSEL, 0, 0);
               sprintf(tempstr, "channel%02d.wav", cursel);

               CreateFilter(filter, 1024,
                  "WAV Files", "*.WAV",
                  "All files (*.*)", "*.*", NULL);

               // setup ofn structure
               SetupOFN(&ofn, OFN_DEFAULTSAVE, hDlg, filter, tempstr, sizeof(tempstr));
               ofn.lpstrDefExt = _16("WAV");

               if (GetSaveFileName(&ofn))
                  ScspSlotDebugAudioSaveWav(cursel, tempstr);

               return TRUE;
            }
            case IDC_SCSPSLOTREGSAVE:
            {
               OPENFILENAME ofn;
               u8 cursel=0;
               WCHAR filter[1024];

               cursel = (u8)SendDlgItemMessage(hDlg, IDC_SCSPSLOTCB, CB_GETCURSEL, 0, 0);
               sprintf(tempstr, "channel%02dregs.bin", cursel);

               CreateFilter(filter, 1024,
                  "Binary Files", "*.BIN",
                  "All files (*.*)", "*.*", NULL);

               // setup ofn structure
               SetupOFN(&ofn, OFN_DEFAULTSAVE, hDlg, filter, tempstr, sizeof(tempstr));
               ofn.lpstrDefExt = _16("BIN");

               if (GetSaveFileName(&ofn))
                  ScspSlotDebugSaveRegisters(cursel, tempstr);

               return TRUE;
            }
            case IDOK:
            {
               EndDialog(hDlg, TRUE);

               return TRUE;
            }
            default: break;
         }
         break;
      }
      case WM_CLOSE:
      {
         EndDialog(hDlg, TRUE);

         return TRUE;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

void SMPCUpdateRegList(HWND hDlg)
{
   char tempstr[128];
   int i;

   SendMessage(GetDlgItem(hDlg, IDC_INPUTREGLB), LB_RESETCONTENT, 0, 0);
   for (i = 0; i < 7; i++)
   {
      sprintf(tempstr, "IREG%d = %02X", i, (int)SmpcRegs->IREG[i]);
      SendMessageA(GetDlgItem(hDlg, IDC_INPUTREGLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
   }

   SendMessage(GetDlgItem(hDlg, IDC_OUTPUTREGLB), LB_RESETCONTENT, 0, 0);

   for (i = 0; i < 10; i++)
   {
      sprintf(tempstr, "OREG%d =  %02X", i, (int)SmpcRegs->OREG[i]);
      SendMessageA(GetDlgItem(hDlg, IDC_OUTPUTREGLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
   }

   for (i = 10; i < 32; i++)
   {
      sprintf(tempstr, "OREG%02d = %02X", i, (int)SmpcRegs->OREG[i]);
      SendMessageA(GetDlgItem(hDlg, IDC_OUTPUTREGLB), LB_ADDSTRING, 0, (LPARAM)tempstr);
   }
}

//////////////////////////////////////////////////////////////////////////////

extern SmpcInternal * SmpcInternalVars;

LRESULT CALLBACK SMPCDebugDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                  LPARAM lParam)
{
   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         char tempstr[1024];
         char *buf;

         SMPCUpdateRegList(hDlg);
         sprintf(tempstr, "Status = %s\r\n", SmpcRegs->SF ? "Busy" : "Ready");
         buf = tempstr + strlen(tempstr);
         if (SmpcRegs->SF)
         {
            sprintf(buf, "Currently executing command: %02X\r\n", SmpcRegs->COMREG);
            sprintf(buf, "time remaining = %ld\r\n", SmpcInternalVars->timing);
         }
         else
            sprintf(buf, "Last executed command: %02X\r\n", SmpcRegs->COMREG);

         SetDlgItemText(hDlg, IDC_SMPCSTATUSET, _16(tempstr));
         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDOK:
            {
               EndDialog(hDlg, TRUE);

               return TRUE;
            }
            default: break;
         }
         break;
      }
      case WM_CLOSE:
      {
         EndDialog(hDlg, TRUE);

         return TRUE;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK GotoAddressDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                    LPARAM lParam)
{
   static u32 *addr;
   char tempstr[9];
   int i;

   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         addr = (u32 *)lParam;
         sprintf(tempstr, "%08lX", addr[0]);
         SetDlgItemText(hDlg, IDC_OFFSETET, _16(tempstr));

         SendDlgItemMessage(hDlg, IDC_SPECIFYADDRRB, BM_SETCHECK, BST_CHECKED, 0);
         SendDlgItemMessage(hDlg, IDC_PRESETADDRRB, BM_SETCHECK, BST_UNCHECKED, 0);

         EnableWindow(GetDlgItem(hDlg, IDC_OFFSETET), TRUE);
         EnableWindow(GetDlgItem(hDlg, IDC_PRESETLISTCB), FALSE);

         SendDlgItemMessage(hDlg, IDC_PRESETLISTCB, CB_RESETCONTENT, 0, 0);
         for (i = 0; i < 13; i++)
         {
            SendDlgItemMessage(hDlg, IDC_PRESETLISTCB, CB_ADDSTRING, 0, (LPARAM)hexaddrlist[i].name);
            if (addr[0] >= hexaddrlist[i].start && addr[0] <= hexaddrlist[i].end)
               SendDlgItemMessage(hDlg, IDC_PRESETLISTCB, CB_SETCURSEL, i, 0);
         }
         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDOK:
            {
               if (SendDlgItemMessage(hDlg, IDC_SPECIFYADDRRB, BM_GETCHECK, 0, 0) == BST_CHECKED)
               {
                  WCHAR tempwstr[MAX_PATH];
                  GetDlgItemText(hDlg, IDC_OFFSETET, tempwstr, 9);
                  wscanf(tempwstr, "%08lX", addr);
               }
               else
                  addr[0] = hexaddrlist[SendDlgItemMessage(hDlg, IDC_PRESETLISTCB, CB_GETCURSEL, 0, 0)].start;
               EndDialog(hDlg, TRUE);
               return TRUE;
            }
            case IDCANCEL:
            {
               EndDialog(hDlg, FALSE);
               return TRUE;
            }
            case IDC_SPECIFYADDRRB:
            {
               if (HIWORD(wParam) == BN_CLICKED)
               {
                  EnableWindow(GetDlgItem(hDlg, IDC_OFFSETET), TRUE);
                  EnableWindow(GetDlgItem(hDlg, IDC_PRESETLISTCB), FALSE);
               }

               break;
            }
            case IDC_PRESETADDRRB:
            {
               if (HIWORD(wParam) == BN_CLICKED)
               {
                  EnableWindow(GetDlgItem(hDlg, IDC_OFFSETET), FALSE);
                  EnableWindow(GetDlgItem(hDlg, IDC_PRESETLISTCB), TRUE);
               }
               break;
            }
            default: break;
         }
         break;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

typedef struct
{
   char searchstr[1024];
   int searchtype;
   u32 startaddr;
   u32 endaddr;
   result_struct *results;
   HWND hDlg;
} searcharg_struct;

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK SearchMemoryDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                     LPARAM lParam)
{
   static searcharg_struct *searcharg;
   char tempstr[10];

   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         int cursel=0;

         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_RESETCONTENT, 0, 0);
         
         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_ADDSTRING, 0, (LPARAM)_16("Hex value(s)"));
         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_ADDSTRING, 0, (LPARAM)_16("Text"));
         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_ADDSTRING, 0, (LPARAM)_16("8-bit Relative value(s)"));
         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_ADDSTRING, 0, (LPARAM)_16("16-bit Relative value(s)"));
         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_ADDSTRING, 0, (LPARAM)_16("Unsigned 8-bit value"));
         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_ADDSTRING, 0, (LPARAM)_16("Signed 8-bit value"));
         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_ADDSTRING, 0, (LPARAM)_16("Unsigned 16-bit value"));
         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_ADDSTRING, 0, (LPARAM)_16("Signed 16-bit value"));
         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_ADDSTRING, 0, (LPARAM)_16("Unsigned 32-bit value"));
         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_ADDSTRING, 0, (LPARAM)_16("Signed 32-bit value"));
         searcharg = (searcharg_struct *)lParam;

         switch (searcharg->searchtype & 0x70)
         {
            case SEARCHSIGNED:
               cursel += 1;
            case SEARCHUNSIGNED:
               cursel += 4 + ((searcharg->searchtype & 0x3) * 2);
               break;
            case SEARCHSTRING:
               cursel = 1;
               break;
            case SEARCHREL8BIT:
               cursel = 2;
               break;
            case SEARCHREL16BIT:
               cursel = 3;
               break;
            case SEARCHHEX:
            default: break;
         }

         SetDlgItemText(hDlg, IDC_SEARCHMEMET, _16(searcharg->searchstr));
         SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_SETCURSEL, cursel, 0);

         sprintf(tempstr, "%08X", (int)searcharg->startaddr);
         SetDlgItemText(hDlg, IDC_SEARCHSTARTADDRET, _16(tempstr));

         sprintf(tempstr, "%08X", (int)searcharg->endaddr);
         SetDlgItemText(hDlg, IDC_SEARCHENDADDRET, _16(tempstr));

         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDOK:
            {
               WCHAR tempwstr[1024];
               int cursel=(int)SendDlgItemMessage(hDlg, IDC_SEARCHTYPECB, CB_GETCURSEL, 0, 0);

               switch(cursel)
               {
                  case 0:
                     searcharg->searchtype = SEARCHHEX;
                     break;
                  case 1:
                     searcharg->searchtype = SEARCHSTRING;
                     break;
                  case 2:
                     searcharg->searchtype = SEARCHREL8BIT;
                     break;
                  case 3:
                     searcharg->searchtype = SEARCHREL16BIT;
                     break;
                  case 4:
                     searcharg->searchtype = SEARCHBYTE | SEARCHUNSIGNED;
                     break;
                  case 5:
                     searcharg->searchtype = SEARCHBYTE | SEARCHSIGNED;
                     break;
                  case 6:
                     searcharg->searchtype = SEARCHWORD | SEARCHUNSIGNED;
                     break;
                  case 7:
                     searcharg->searchtype = SEARCHWORD | SEARCHSIGNED;
                     break;
                  case 8:
                     searcharg->searchtype = SEARCHLONG | SEARCHUNSIGNED;
                     break;
                  case 9:
                     searcharg->searchtype = SEARCHLONG | SEARCHSIGNED;
                     break;
               }

               GetDlgItemText(hDlg, IDC_SEARCHMEMET, tempwstr, 1024);
               WideCharToMultiByte(CP_ACP, 0, tempwstr, -1, searcharg->searchstr, 1024, NULL, NULL);
               EndDialog(hDlg, TRUE);
               return TRUE;
            }
            case IDCANCEL:
            {
               EndDialog(hDlg, FALSE);
               return TRUE;
            }
            default: break;
         }
         break;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

static int KillSearchThread=0;
static HANDLE hThread=INVALID_HANDLE_VALUE;
static DWORD thread_id;
#define SEARCHSIZE      0x10000

DWORD WINAPI __stdcall SearchThread(void *b)
{    
   result_struct *results;
   u32 startaddr;
   u32 endaddr;
   searcharg_struct *searcharg=(searcharg_struct *)b;

   startaddr=searcharg->startaddr;
   endaddr=searcharg->endaddr;

   PostMessage(GetDlgItem(searcharg->hDlg, IDC_SEARCHPB), PBM_SETRANGE, 0, MAKELPARAM (0, (endaddr-startaddr) / SEARCHSIZE));
   PostMessage(GetDlgItem(searcharg->hDlg, IDC_SEARCHPB), PBM_SETSTEP, 1, 0);

   while (KillSearchThread != 1)
   {
      u32 numresults=1;

      if ((searcharg->endaddr - startaddr) > SEARCHSIZE)
         endaddr = startaddr+SEARCHSIZE;
      else
         endaddr = searcharg->endaddr;

      results = MappedMemorySearch(startaddr, endaddr,
                                   searcharg->searchtype | SEARCHEXACT,
                                   searcharg->searchstr,
                                   NULL, &numresults);
      if (results && numresults)
      {
         // We're done
         searcharg->results = results;          
         EndDialog(searcharg->hDlg, TRUE);
         return 0;
      }

      if (results)
         free(results);

      startaddr += (endaddr - startaddr);
      if (startaddr >= searcharg->endaddr)
      {
         EndDialog(searcharg->hDlg, TRUE);
         searcharg->results = NULL;
         return 0;
      }

      PostMessage(GetDlgItem(searcharg->hDlg, IDC_SEARCHPB), PBM_STEPIT, 0, 0);
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK SearchBusyDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                   LPARAM lParam)
{
   static searcharg_struct *searcharg;

   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         // Create thread
         KillSearchThread=0;
         searcharg = (searcharg_struct *)lParam;
         searcharg->hDlg = hDlg;
         hThread = CreateThread(NULL,0,(LPTHREAD_START_ROUTINE) SearchThread,(void *)lParam,0,&thread_id);
         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDCANCEL:
            {
               // Kill thread
               KillSearchThread = 1;
               if (WaitForSingleObject(hThread, INFINITE) == WAIT_TIMEOUT)
               {
                  // Couldn't close thread cleanly
                  TerminateThread(hThread,0);                                  
               }          
               CloseHandle(hThread);
               hThread = INVALID_HANDLE_VALUE;
               searcharg->results = NULL;
               EndDialog(hDlg, FALSE);
               return TRUE;
            }
            default: break;
         }
         break;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK MemoryEditorDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                                     LPARAM lParam)
{
   static searcharg_struct searcharg;

   switch (uMsg)
   {
      case WM_INITDIALOG:
      {
         SendDlgItemMessage(hDlg, IDC_HEXEDIT, HEX_SETADDRESSLIST, 13, (LPARAM)hexaddrlist);
         searcharg.startaddr = 0;
         searcharg.endaddr = 0x06100000;
         return TRUE;
      }
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDOK:
            {
               EndDialog(hDlg, TRUE);

               return TRUE;
            }
            case IDC_GOTOADDRESS:
            {
               u32 addr=0x06000000;

               if (DialogBoxParam(y_hInstance, MAKEINTRESOURCE(IDD_GOTOADDRESS), hDlg, (DLGPROC)GotoAddressDlgProc, (LPARAM)&addr) == TRUE)
               {
                  SendDlgItemMessage(hDlg, IDC_HEXEDIT, HEX_GOTOADDRESS, 0, addr);
                  SendMessage(hDlg, WM_NEXTDLGCTL, IDC_HEXEDIT, TRUE);
               }
               break;
            }
            case IDC_SEARCHMEM:
            {
               searcharg.startaddr = (u32)SendDlgItemMessage(hDlg, IDC_HEXEDIT, HEX_GETCURADDRESS, 0, 0);
               
               if (DialogBoxParam(y_hInstance, MAKEINTRESOURCE(IDD_SEARCHMEMORY), hDlg,
                                  (DLGPROC)SearchMemoryDlgProc,
                                  (LPARAM)&searcharg) == TRUE)
               {
                  // Open up searching dialog
                  if (DialogBoxParam(y_hInstance, MAKEINTRESOURCE(IDD_SEARCHBUSY), hDlg,
                                     (DLGPROC)SearchBusyDlgProc,
                                     (LPARAM)&searcharg) == TRUE)
                  {
                     if (searcharg.results)
                     {
                        // Ok, we found a match, go to that address
                        SendDlgItemMessage(hDlg, IDC_HEXEDIT, HEX_GOTOADDRESS, 0, searcharg.results[0].addr);
                        free(searcharg.results);
                     }
                     else
                     {
                        // No matches found, if the search wasn't from bios start,
                        // ask the user if they want to search from the begining.

                        if (SendDlgItemMessage(hDlg, IDC_HEXEDIT, HEX_GETCURADDRESS, 0, 0) != 0)
                           MessageBox (hDlg, _16("Finished searching up to end of memory, continue from the beginning?"), _16("Wrap search?"), MB_OKCANCEL);
                        else
                           MessageBox (hDlg, _16("No matches found"), _16("Finished search"), MB_OK);
                     }
                  }
               }
               break;
            }
            default: break;
         }
         break;
      }
      case WM_CLOSE:
      {
         EndDialog(hDlg, TRUE);

         return TRUE;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK LogDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam,
                            LPARAM lParam)
{
   switch (uMsg)
   {
      case WM_INITDIALOG:
         // Use the maximum available characters
         SendDlgItemMessage(hDlg, IDC_LOGET, EM_LIMITTEXT, 0, 0); 
         return TRUE;
      case WM_COMMAND:
      {
         switch (LOWORD(wParam))
         {
            case IDC_CLEARBT:
               SetDlgItemText(hDlg, IDC_LOGET, _16(""));
               return TRUE;
            case IDC_SAVELOGBT:
            {
               OPENFILENAME ofn;
               WCHAR filter[1024];

               CreateFilter(filter, 1024,
                  "Text Files", "*.txt",
                  "All files (*.*)", "*.*", NULL);

               // setup ofn structure
               SetupOFN(&ofn, OFN_DEFAULTSAVE, hDlg, filter, logfilename, sizeof(logfilename));
               ofn.lpstrDefExt = _16("TXT");

               if (GetSaveFileName(&ofn))
               {
                  FILE *fp=fopen(logfilename, "wb");
                  HLOCAL *localbuf=(HLOCAL *)SendDlgItemMessage(hDlg, IDC_LOGET, EM_GETHANDLE, 0, 0);
                  unsigned char *buf;

                  if (fp == NULL)
                  {
                     MessageBox (hDlg, _16("Unable to open file for writing"), _16("Error"),  MB_OK | MB_ICONINFORMATION);
                     return FALSE;
                  }

                  buf = LocalLock(localbuf);
                  fwrite((void *)buf, 1, strlen(buf), fp);
                  LocalUnlock(localbuf);
                  fclose(fp);
               }

               return TRUE;
            }
            default: break;
         }
         break;
      }
      case WM_DESTROY:
      {
         KillTimer(hDlg, 1);
         break;
      }
      default: break;
   }

   return FALSE;
}

//////////////////////////////////////////////////////////////////////////////

void UpdateLogCallback (char *string)
{
   int len = GetWindowTextLength(GetDlgItem(LogWin, IDC_LOGET));
   sprintf(logbuffer, "%s\r\n", string);
   SendDlgItemMessage(LogWin, IDC_LOGET, EM_SETSEL, len, len);
   SendDlgItemMessage(LogWin, IDC_LOGET, EM_REPLACESEL, FALSE, (LPARAM)_16(logbuffer));  
}

//////////////////////////////////////////////////////////////////////////////
