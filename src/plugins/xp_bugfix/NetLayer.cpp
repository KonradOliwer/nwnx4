/***************************************************************************
    NetLayer - Wrapper for AuroraServerNetLayer.dll and hooks into NWN2 to
	use it.

    Copyright (C) 2009 Skywing (skywing@valhallalegends.com).  This instance
    of NetLayerWindow is licensed under the GPLv2 for the usage of the NWNX4
    project, nonwithstanding other licenses granted by the copyright holder.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

***************************************************************************/

#include "bugfix.h"
#include "..\..\misc\Patch.h"
#include "ServerNetLayer.h"
#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>
#include "NetLayer.h"

extern long GameObjUpdateBurstSize;

bool ReplaceNetLayer();

CNetLayerInternal * NetLayerInternal;


HMODULE AuroraServerNetLayer;

struct PlayerStateInfo
{
	bool AreaLoadPending;
	bool BlockGameObjUpdate;
};

NETLAYER_HANDLE Connections[MAX_PLAYERS];
PlayerStateInfo PlayerState[MAX_PLAYERS];

typedef
void
(__stdcall * OnPlayerConnectionCloseProc)(
	__in unsigned long PlayerId,
	__in void * Context
	);

typedef
BOOL
(__stdcall * OnPlayerConnectionReceiveProc)(
	__in unsigned long PlayerId,
	__in_bcount( Length ) const unsigned char * Data,
	__in size_t Length,
	__in void * Context
	);

typedef
BOOL
(__stdcall * OnPlayerConnectionSendProc)(
	__in unsigned long PlayerId,
	__in_bcount( Size ) unsigned char * Data,
	__in unsigned long Size,
	__in unsigned long Flags,
	__in void * Context
	);



AuroraServerNetLayerCreateProc  AuroraServerNetLayerCreate_;
AuroraServerNetLayerSendProc    AuroraServerNetLayerSend_;
AuroraServerNetLayerReceiveProc AuroraServerNetLayerReceive_;
AuroraServerNetLayerTimeoutProc AuroraServerNetLayerTimeout_;
AuroraServerNetLayerDestroyProc AuroraServerNetLayerDestroy_;
AuroraServerNetLayerQueryProc   AuroraServerNetLayerQuery_;

//
// Packet filtering callouts.
//

void *                          PacketFilterContext;
OnPlayerConnectionCloseProc     OnPlayerConnectionClose;
OnPlayerConnectionReceiveProc   OnPlayerConnectionReceive;
OnPlayerConnectionSendProc      OnPlayerConnectionSend;

/***************************************************************************
    Debug output to the debugger before we can use wx logging safely.
***************************************************************************/

void
DebugPrintV(
	__in const char *Format,
	__in va_list Ap
	)
{
	CHAR Message[ 4096 ];

	//
	// Let's not clutter up things if a user mode debugger isn't present.
	//

	if (!IsDebuggerPresent())
		return;

	StringCbVPrintfA(
		Message,
		sizeof( Message ),
		Format,
		Ap
		);

	OutputDebugStringA( Message );
}

void
DebugPrint(
	__in const char *Format,
	...
	)
{
	va_list Ap;

	va_start( Ap, Format );

	DebugPrintV( Format, Ap );

	va_end( Ap );
}


void
__stdcall
SetPacketFilterCallouts(
	__in void * Context,
	__in OnPlayerConnectionCloseProc OnClose,
	__in OnPlayerConnectionReceiveProc OnReceive,
	__in OnPlayerConnectionSendProc OnSend
	)
{
	PacketFilterContext       = Context;
	OnPlayerConnectionClose   = OnClose;
	OnPlayerConnectionReceive = OnReceive;
	OnPlayerConnectionSend    = OnSend;
}

const char *
__stdcall
GetPlayerAccountName(
	__in unsigned long PlayerId
	)
{
	if (PlayerId > MAX_PLAYERS)
		return false;

	if (!NetLayerInternal->Players[PlayerId].m_bPlayerInUse)
		return false;

	if (NetLayerInternal->Players[PlayerId].m_sPlayerName.m_sString == NULL)
		return "";
	else
		return NetLayerInternal->Players[PlayerId].m_sPlayerName.m_sString;
}


bool PlayerIdToConnectionId(unsigned long PlayerId, unsigned long *ConnectionId)
{
	unsigned long idx;

	if (PlayerId > MAX_PLAYERS)
		return false;

	if (!NetLayerInternal->Players[PlayerId].m_bPlayerInUse)
		return false;

	idx = NetLayerInternal->Players[PlayerId].m_nSlidingWindowId;

	if (!NetLayerInternal->Windows[idx].m_WindowInUse)
		return false;

	*ConnectionId = NetLayerInternal->Windows[idx].m_ReceiveConnectionId;

	return true;
}

bool PlayerIdToSlidingWindow(unsigned long PlayerId, unsigned long *SlidingWindowId)
{
	if (PlayerId > MAX_PLAYERS)
		return false;

	for (unsigned long i = 0; i < MAX_PLAYERS; i += 1)
	{
		if (NetLayerInternal->Windows[i].m_WindowInUse != 1)
			continue;

		if (NetLayerInternal->Windows[i].m_PlayerId == PlayerId)
		{
			*SlidingWindowId = i;
			return true;
		}
	}

	return false;
}

/*

0:000> u 004ff8a0 
nwn2server!CNetLayerInternal::PlayerIdToConnectionId:
004ff8a0 8b442404        mov     eax,dword ptr [esp+4]
004ff8a4 8bd0            mov     edx,eax
004ff8a6 c1e204          shl     edx,4
004ff8a9 2bd0            sub     edx,eax
004ff8ab 83bcd19076030000 cmp     dword ptr [ecx+edx*8+37690h],0
004ff8b3 8d04d1          lea     eax,[ecx+edx*8]
004ff8b6 7425            je      nwn2server!CNetLayerInternal::PlayerIdToConnectionId+0x3d (004ff8dd)
004ff8b8 8b80a0760300    mov     eax,dword ptr [eax+376A0h]
0:000> u
nwn2server!CNetLayerInternal::PlayerIdToConnectionId+0x1e:
004ff8be 69c03c090000    imul    eax,eax,93Ch
004ff8c4 03c1            add     eax,ecx
004ff8c6 83781400        cmp     dword ptr [eax+14h],0
004ff8ca 7411            je      nwn2server!CNetLayerInternal::PlayerIdToConnectionId+0x3d (004ff8dd)
004ff8cc 8b401c          mov     eax,dword ptr [eax+1Ch]
004ff8cf 8b4c2408        mov     ecx,dword ptr [esp+8]
004ff8d3 8901            mov     dword ptr [ecx],eax
004ff8d5 b801000000      mov     eax,1
0:000> u
nwn2server!CNetLayerInternal::PlayerIdToConnectionId+0x3a:
004ff8da c20800          ret     8
004ff8dd 33c0            xor     eax,eax
004ff8df c20800          ret     8
004ff8e2 cc              int     3
004ff8e3 cc              int     3
004ff8e4 cc              int     3
004ff8e5 cc              int     3
004ff8e6 cc              int     3
*/


BOOL
__stdcall
SendMessageToPlayer(
	__in unsigned long PlayerId,
	__in_bcount( Size ) unsigned char * Data,
	__in unsigned long Size,
	__in unsigned long Flags
	);

BOOL
__stdcall
SendMessageToPlayer2(
	__in unsigned long PlayerId,
	__in_bcount( Size ) unsigned char * Data,
	__in unsigned long Size,
	__in unsigned long Flags
	);

BOOL
__stdcall
FrameTimeout2(
	__in unsigned long Unused
	);

BOOL
__stdcall
FrameReceive2(
	__in_bcount( Size ) unsigned char *Data,
	__in unsigned long Size
	);

void
SetGameObjUpdateSize(
	);

Patch _patches2[] =
{
	Patch( OFFS_SendMessageToPlayer, "\xe9", 1 ),
	Patch( OFFS_SendMessageToPlayer+1, (relativefunc)SendMessageToPlayer2 ),
	Patch( OFFS_CallFrameTimeout, (relativefunc)FrameTimeout2 ),
	Patch( OFFS_FrameReceive, "\xe9", 1 ),
	Patch( OFFS_FrameReceive+1, (relativefunc)FrameReceive2 ),
	Patch( OFFS_FrameSend, "\xc2\x0c\x00", 3 ), // Disable all outbound sends (from FrameTimeout)
	Patch( OFFS_GameObjUpdateSizeLimit1, "\xe9", 1 ),
	Patch( OFFS_GameObjUpdateSizeLimit1+1, (relativefunc) SetGameObjUpdateSize ),

	Patch( )
};

Patch *patches2 = _patches2;

bool
__cdecl
OnNetLayerWindowReceive(
	__in_bcount( Length ) const unsigned char * Data,
	__in size_t Length,
	__in void * Context
	);

bool
__cdecl
OnNetLayerWindowSend(
	__in_bcount( Length ) const unsigned char * Data,
	__in size_t Length,
	__in void * Context
	);

bool
__cdecl
OnNetLayerWindowStreamError(
	__in bool Fatal,
	__in unsigned long ErrorCode,
	__in void * Context
	);

void ResetWindow(unsigned long PlayerId)
{
	CONNECTION_CALLBACKS Callbacks;
	NETLAYER_HANDLE      Handle;

	Callbacks.Context       = (void *) (ULONG_PTR) PlayerId;
	Callbacks.OnReceive     = OnNetLayerWindowReceive;
	Callbacks.OnSend        = OnNetLayerWindowSend;
	Callbacks.OnStreamError = OnNetLayerWindowStreamError;

	PlayerState[PlayerId].AreaLoadPending    = true;
	PlayerState[PlayerId].BlockGameObjUpdate = true;

	//
	// Create the window (the first time around), else simply reset its
	// internal state if we have already set it up.  We do not need to allocate
	// a new instance to reset its state and instead pass in the previous
	// instance handle.
	//

	Handle = AuroraServerNetLayerCreate_(Connections[PlayerId], &Callbacks);

	if (Handle == NULL)
		return;

	Connections[PlayerId] = Handle;
}

bool ReplaceNetLayer()
{
	//
	// Wire up the dllimports.
	//

	struct { bool Required; const char *Name; void **Import; } DllImports[] =
	{
		{ true , "AuroraServerNetLayerCreate",  (void **) &AuroraServerNetLayerCreate_  },
		{ true , "AuroraServerNetLayerSend",    (void **) &AuroraServerNetLayerSend_    },
		{ true , "AuroraServerNetLayerReceive", (void **) &AuroraServerNetLayerReceive_ },
		{ true , "AuroraServerNetLayerTimeout", (void **) &AuroraServerNetLayerTimeout_ },
		{ true , "AuroraServerNetLayerDestroy", (void **) &AuroraServerNetLayerDestroy_ },
		{ false, "AuroraServerNetLayerQuery",   (void **) &AuroraServerNetLayerQuery_   }
	};
	AuroraServerNetLayer = LoadLibrary("AuroraServerNetLayer.dll");

	if (!AuroraServerNetLayer)
	{
		wxLogMessage(wxT("* Failed to load AuroraServerNetLayer.dll"));
		return false;
	}

	for (int i = 0; i < sizeof(DllImports)/sizeof(DllImports[0]); i += 1)
	{
		*DllImports[i].Import = (void *)GetProcAddress(AuroraServerNetLayer, DllImports[i].Name);

		if (!*DllImports[i].Import)
		{
			if (!DllImports[i].Required)
			{
				wxLogMessage(
					wxT("* Warning: You need to update your AuroraServerNetLayer.dll; missing optional entrypoint AuroraServerNetLayer!%s"),
					DllImports[i].Name);
				continue;
			}
			wxLogMessage(wxT("* Unable to resolve AuroraServerNetLayer!%s"), DllImports[i].Name);
			return false;
		}
	}

	int i = 0;
	while(patches2[i].Apply()) {
		i++;
	}

	//
	// Set each window into a good initial state.
	//

	for (unsigned long i = 0; i < MAX_PLAYERS; i += 1)
		ResetWindow( i );

	//
	// All done.
	//

	return true;
}

BOOL
__stdcall
SendMessageToPlayer(
	__in unsigned long PlayerId,
	__in_bcount( Size ) unsigned char * Data,
	__in unsigned long Size,
	__in unsigned long Flags
	)
{
	DebugPrint( "SendMessageToPlayer(%08X, %p, %08X, %08X)\n", PlayerId, Data, Size, Flags);

	//
	// Enqueue the outbound message to each applicable player's internal
	// NetLayerWindow object.
	//

	for (unsigned Player = 0; Player < MAX_PLAYERS; Player += 1)
	{
		//
		// Ignore players that are not present.
		//

		if (!NetLayerInternal->Players[Player].m_bPlayerInUse)
			continue;

		//
		// Check if this player matches the given filter.  We will continue the
		// next loop iteration if it does not.
		//

		switch (PlayerId)
		{

		case PLAYERID_SERVER:
			DebugPrint("Send to server...\n");
			return TRUE;

		case PLAYERID_ALL_PLAYERS:
			if (!NetLayerInternal->Players[Player].m_bPlayerPrivileges)
				continue;
			break;

		case PLAYERID_ALL_GAMEMASTERS:
			if (!NetLayerInternal->Players[Player].m_bGameMasterPrivileges)
				continue;
			break;

		case PLAYERID_ALL_SERVERADMINS:
			if (!NetLayerInternal->Players[Player].m_bServerAdminPrivileges)
				continue;
			break;

		default: // Specific player id and not a symbolic filter
			if (PlayerId != Player)
				continue;

			//
			// Fallthrough.
			//

		case PLAYERID_ALL_CLIENTS: // Anyone, even if they are not a player yet
			break;

		}

		if (Size >= 0x03 && Data[0] == 0x50)
		{
			DebugPrint("Send %s.%02X to player %lu (%lu bytes)\n", GetMsgName(Data[1]), Data[2], Player, Size);

			switch (Data[1])
			{

			case CMD::Login:
			case CMD::Area:
			case CMD::GameObjUpdate:
			case CMD::Chat:
			case CMD::ClientSideMessage:
			case CMD::VoiceChat:
			case CMD::QuickChat:
			case CMD::CustomAnim:
			case CMD::DungeonMaster:
			case CMD::CharList:
				Flags |= SEND_FLUSH_CACHE;
				break;

			}

			//
			// Record if this player is about to join an area, or if the first
			// GameObjUpdate was sent after joining the area.  This lets us
			// temporarily accelerate GameObjUpdates for players who are
			// downloading the initial area contents all at once, but only for
			// the first update -- even if their window was already full from
			// the already queued static area contents being sent via the
			// Area.ClientArea message.
			//

			if ((Data[1] == CMD::Area) &&
			    (Data[2] == 0x01)) // ClientArea
			{
				DebugPrint("Enabling accelerated area transfer to player %lu.\n", Player);

				//
				// Enable bursting for area load, and unblock GameObjUpdates.
				//

				PlayerState[Player].AreaLoadPending    = true;
				PlayerState[Player].BlockGameObjUpdate = false;
			}
			else if ((Data[1] == CMD::GameObjUpdate) &&
				     (Data[2] == 0x01)) // Update
			{
				//
				// If GameObjUpdates are still being blocked, then drop the
				// message.  This prevents the client from being crashed by the
				// server erroneously emitting GameObjUpdates prior to the
				// transmission of an Area.ClientArea message.
				//
				// The server will once again retransmit the entire area
				// contents once Area.ClientArea is signaled, so it is not
				// harmful to eliminate these extraneous updates.
				//

				if (PlayerState[Player].BlockGameObjUpdate)
				{
					DebugPrint("Blocking premature GameObjUpdate.Update to player %lu.\n", Player);
					continue;
				}

				DebugPrint("Closing accelerated area transfer window for player %lu.\n", Player);
				PlayerState[Player].AreaLoadPending = false;
			}
		}

		//
		// Call the packet filter, if one was registered.
		//

		if (OnPlayerConnectionSend != NULL)
		{
			if (OnPlayerConnectionSend(
				Player,
				Data,
				Size,
				Flags,
				PacketFilterContext) == FALSE)
			{
				DebugPrint("Packet filter blocked message send.\n");
				return TRUE;
			}
		}

		AuroraServerNetLayerSend_(
			Connections[Player],
			Data,
			Size,
			FALSE,
			(Flags & SEND_FLUSH_CACHE) ? TRUE : FALSE);

	}

	return TRUE;
}

void
CheckForNewWindow(
	__in SlidingWindow *Winfo
	)
{
	//
	// Check if this CNetLayerWindow instance has not yet been tagged with our
	// magical SEQ/ACK values.  These are reset to zero on ShutDown/Initialize
	// as called by the game, but they should never increment with receive
	// processing never hit and the data send path also never hit.
	//

	if ((Winfo->m_LastSeqAcked == 0x0000) &&
		(Winfo->m_Seq == 0x0000)          &&
		(Winfo->m_RemoteSeq == 0x0000))
	{
		DebugPrint( "Reinitializing sliding window %p for player %lu.\n", Winfo, Winfo->m_PlayerId );

		if (!Winfo->m_WindowInUse)
		{
			DebugPrint( "!!! Window isn't in use!\n" );

			if (IsDebuggerPresent( ))
				__debugbreak( );
		}
		else if (Winfo->m_PlayerId >= MAX_PLAYERS)
		{
			DebugPrint( "!!! PlayerId is out of range for window!\n" );

			if (IsDebuggerPresent( ))
				__debugbreak( );

			return;
		}

		//
		// Call the packet filter, if one was registered.
		//

		if (OnPlayerConnectionClose != NULL)
		{
			OnPlayerConnectionClose(
				Winfo->m_PlayerId,
				PacketFilterContext);
		}

		//
		// Reinitialize our internal window so that it has the right seq/ack
		// states.
		//

		ResetWindow( Winfo->m_PlayerId );

		//
		// Tag this window as owned by us so that we do not try and throw away
		// our existing seq/ack state the next time around.  This also lets us
		// easily verify that nobody else is touching the window's internal
		// state as we should have disabled all of that logic.
		//

		Winfo->m_Seq = 0x4242;
	}
	else if ((Winfo->m_LastSeqAcked == 0x0000) &&
		     (Winfo->m_Seq == 0x4242)          &&
		     (Winfo->m_RemoteSeq == 0x0000))
	{
		//
		// Everything checks out as ok as a window that we have already set up
		// for our end, consider it good.
		//

		return;
	}
	else
	{
		//
		// With us having snipped out the code that would update seq/ack fields
		// this should not happen unless there's something we've missed, which
		// would be bad.  Catch this here and now so we can figure out just
		// what happened.
		//

		DebugPrint(
			"Window %p player %lu LastSeqAcked %04X Seq %04X RemoteSeq %04X has had its internal seq/ack state modified when it should not have!\n",
			Winfo,
			Winfo->m_PlayerId,
			Winfo->m_LastSeqAcked,
			Winfo->m_Seq,
			Winfo->m_RemoteSeq);

		if (IsDebuggerPresent( ))
			__debugbreak( );
	}
}

__declspec(naked)
BOOL
__stdcall
SendMessageToPlayer2(
	__in unsigned long PlayerId,
	__in_bcount( Size ) unsigned char * Data,
	__in unsigned long Size,
	__in unsigned long Flags
	)
{
	__asm
	{
		mov  [NetLayerInternal], ecx
		jmp  SendMessageToPlayer
	}
}

__declspec(naked)
void
__fastcall
SetInFrameTimer(
	__in SlidingWindow * Winfo
	)
{
	__asm
	{
		; ecx already set by __fastcall
		mov   eax, OFFS_SetInFrameTimer
		jmp   eax
	}
}

__declspec(naked)
void
__stdcall
CallFrameTimeout(
	__in unsigned long Unused,
	__in SlidingWindow * Winfo
	)
{
	__asm
	{
		push    ebp
		mov     ebp, esp

		push    dword ptr [ebp+08h]
		mov     ecx, dword ptr [ebp+0ch]
		mov     eax, OFFS_FrameTimeout
		call    eax

		mov     esp, ebp
		pop     ebp
		ret     08h
	}
}

BOOL
__stdcall
FrameReceive(
	__in_bcount( Size ) unsigned char *Data,
	__in unsigned long Size,
	__in SlidingWindow *Winfo
	)
{
	DebugPrint(
		"FrameReceive: Recv from SlidingWindow %p player %lu\n",
		Winfo,
		Winfo->m_PlayerId);

	//
	// Drop the message if we haven't initialized fully.  It will be resent
	// anyway so this is ok.
	//

	if (NetLayerInternal == NULL)
		return TRUE;

	//
	// Reinitialize the window if appropriate.
	//

	CheckForNewWindow( Winfo );

	//
	// Update timeouts so that the server doesn't drop this player for timeout.
	//

	SetInFrameTimer( Winfo );

	//
	// Pass it on to the internal NetLayerWindow implementation to deal with.
	//

	return AuroraServerNetLayerReceive_(
		Connections[Winfo->m_PlayerId],
		Data,
		(size_t) Size);
}

__declspec(naked)
BOOL
__stdcall
FrameReceive2(
	__in_bcount( Size ) unsigned char *Data,
	__in unsigned long Size
	)
{
	__asm
	{
		push    ebp
		mov     ebp, esp

		push    ecx
		push    dword ptr [ebp+0ch]
		push    dword ptr [ebp+08h]
		call    FrameReceive

		mov     esp, ebp
		pop     ebp
		ret     08h
	}
}

BOOL
__stdcall
FrameTimeout(
	__in unsigned long Unused,
	__in SlidingWindow *Winfo
	)
{
	//DebugPrint(
	//	"FrameTimeout: Do timer processing for SlidingWindow %p player %lu\n",
	//	Winfo,
	//	Winfo->m_PlayerId);

	//
	// Reinitialize the window if appropriate.
	//

	CheckForNewWindow( Winfo );

	//
	// Perform timeout handling in the replacement NetLayerWindow.
	//

	AuroraServerNetLayerTimeout_(Connections[Winfo->m_PlayerId]);

	//
	// Let the server do its internal timeout handling too, so that it may drop
	// a player that has become unresponsive.  N.B.  We've neutered the
	// FrameSend API, so FrameTimeout's attempts to send a NAK/ACK will not be
	// harmful.  It will still be able to call DisconnectPlayer as appropriate.
	//

	CallFrameTimeout( Unused, Winfo );

	return TRUE;
}

__declspec(naked)
BOOL
__stdcall
FrameTimeout2(
	__in unsigned long Unused
	)
{
	__asm
	{
		push    ebp
		mov     ebp, esp

		push    ecx
		push    dword ptr [ebp+08h]
		call    FrameTimeout

		mov     esp, ebp
		pop     ebp
		ret     04h
	}
}



__declspec(naked)
BOOL
__stdcall
CallHandleMessage(
	__in_bcount( Length ) const unsigned char * Data,
	__in size_t Length,
	__in unsigned long PlayerId
	)
{
	__asm
	{
		;
		; BOOL
		; CServerExoApp::HandleMessage(
		;   __in unsigned long PlayerId,
		;   __in_bcount( Length ) const unsigned char * Data,
		;   __in size_t Size,
		;   __in bool NonClientMessage
		;   );
		;

		push    ebp
		mov     ebp, esp

		push    0h
		push    dword ptr [ebp+0ch]
		push    dword ptr [ebp+08h]
		push    dword ptr [ebp+10h]

		mov     ecx, dword ptr [NetLayerInternal]
		mov     ecx, dword ptr [ecx]
		mov     edx, dword ptr [ecx]
		call    dword ptr [edx+10h]

		mov     esp, ebp
		pop     ebp
		ret     0ch
	}
}

bool
__cdecl
OnNetLayerWindowReceive(
	__in_bcount( Length ) const unsigned char * Data,
	__in size_t Length,
	__in void * Context
	)
{
	unsigned long     PlayerId = (unsigned long) (ULONG_PTR) Context;
	PlayerInfo      * Pinfo;
	SlidingWindow   * Winfo;

	Pinfo = &NetLayerInternal->Players[ PlayerId ];
	Winfo = &NetLayerInternal->Windows[ Pinfo->m_nSlidingWindowId ];

	if (Length >= 0x03)
	{
		DebugPrint(
			"OnNetLayerWindowReceive: Recv %s.%02X from player %lu WindowId %lu\n",
			GetMsgName(Data[1]),
			Data[2],
			PlayerId,
			Pinfo->m_nSlidingWindowId);
	}

	if (!Pinfo->m_bPlayerInUse)
	{
		DebugPrint( "OnNetLayerWindowReceive: Player not active!\n" );
		return false;
	}

	if (Winfo->m_PlayerId != PlayerId)
	{
		wxLogMessage(wxT("OnNetLayerWindowReceive: *** SLIDING WINDOW PLAYER ID MISMATCH !! %lu, %lu, %lu (%p, %p)"),
			PlayerId,
			Winfo->m_PlayerId,
			Pinfo->m_nSlidingWindowId,
			Pinfo,
			Winfo);
		DebugPrint(
			"OnNetLayerWindowReceive: *** SLIDING WINDOW PLAYER ID MISMATCH !! %lu, %lu, %lu (%p, %p)\n",
			PlayerId,
			Winfo->m_PlayerId,
			Pinfo->m_nSlidingWindowId,
			Pinfo,
			Winfo);

		if (IsDebuggerPresent( ))
			__debugbreak( );

		return false;
	}

	//
	// Call the packet filter, if one was registered.
	//

	if (OnPlayerConnectionReceive != NULL)
	{
		if (OnPlayerConnectionReceive(
			PlayerId,
			Data,
			Length,
			PacketFilterContext) == FALSE)
		{
			DebugPrint("Packet filter blocked message receive.\n");
			return true;
		}
	}

	//
	// Pass the packet on to the actual handler, indicating it up to the game
	// for completion handling.
	//

	return CallHandleMessage( Data, Length, PlayerId ) ? TRUE : FALSE;
}

bool
__cdecl
OnNetLayerWindowSend(
	__in_bcount( Length ) const unsigned char * Data,
	__in size_t Length,
	__in void * Context
	)
{
	unsigned long     PlayerId = (unsigned long) (ULONG_PTR) Context;
	PlayerInfo      * Pinfo;
	SlidingWindow   * Winfo;
	CExoNetInternal * NetI;
	sockaddr_in       sin;
	int               slen;

	//
	// Drop the message if we haven't initialized fully.  It will be resent
	// anyway so this is ok.
	//

	if (NetLayerInternal == NULL)
		return true;

	Pinfo = &NetLayerInternal->Players[ PlayerId ];
	Winfo = &NetLayerInternal->Windows[ Pinfo->m_nSlidingWindowId ];

	DebugPrint( "OnNetLayerWindowSend: Send to player %lu WindowId %lu %lu bytes\n", PlayerId, Pinfo->m_nSlidingWindowId, Length );
//	wxLogMessage( wxT("OnNetLayerWindowSend: Send to player %lu WindowId %lu %lu bytes\n"), PlayerId, Pinfo->m_nSlidingWindowId, Length );

	if (!Pinfo->m_bPlayerInUse)
	{
		DebugPrint( "OnNetLayerWindowSend: Player not active!\n" );
		return false;
	}

	if (Winfo->m_PlayerId != PlayerId)
	{
		wxLogMessage(wxT("OnNetLayerWindowSend: *** SLIDING WINDOW PLAYER ID MISMATCH !! %lu, %lu, %lu (%p, %p)"),
			PlayerId,
			Winfo->m_PlayerId,
			Pinfo->m_nSlidingWindowId,
			Pinfo,
			Winfo);
		DebugPrint(
			"OnNetLayerWindowSend: *** SLIDING WINDOW PLAYER ID MISMATCH !! %lu, %lu, %lu (%p, %p)\n",
			PlayerId,
			Winfo->m_PlayerId,
			Pinfo->m_nSlidingWindowId,
			Pinfo,
			Winfo);

		if (IsDebuggerPresent( ))
			__debugbreak( );

		return false;
	}

	NetI = NetLayerInternal->Net->Internal;

	if (Winfo->m_ConnectionId >= NetI->NumConnections)
	{
		wxLogMessage(wxT("OnNetLayerWindowSend: *** ConnectionId for player %lu window %lu out of range !! %lu >= %lu (NetI %p)"),
			PlayerId,
			Pinfo->m_nSlidingWindowId,
			Winfo->m_ConnectionId,
			NetI->NumConnections);
		DebugPrint("OnNetLayerWindowSend: *** ConnectionId for player %lu window %lu out of range !! %lu >= %lu (NetI %p)\n",
			PlayerId,
			Pinfo->m_nSlidingWindowId,
			Winfo->m_ConnectionId,
			NetI->NumConnections);

		if (IsDebuggerPresent( ))
			__debugbreak( );

		return false;
	}

	//
	// Write the raw message out using the game's sendto socket and the address
	// information for this player.
	//

	ZeroMemory( sin.sin_zero, sizeof( sin.sin_zero ) );
	sin.sin_family      = AF_INET;
	sin.sin_port        = htons( NetI->ConnectionAddresses[ Winfo->m_ConnectionId ].Port );
	sin.sin_addr.s_addr = NetI->ConnectionAddresses[ Winfo->m_ConnectionId ].Address;

	DebugPrint( "Send to %08X:%lu:\n", sin.sin_addr.s_addr, NetI->ConnectionAddresses[ Winfo->m_ConnectionId ].Port );

	slen = sendto(
		NetI->Socket,
		(const char *) Data,
		(int) Length,
		0,
		(sockaddr *) &sin,
		sizeof( sin ));

	if (slen < 0)
	{
		DebugPrint( "sendto fails - %lu\n", WSAGetLastError( ) );
		return false;
	}

	return true;
}

bool
__cdecl
OnNetLayerWindowStreamError(
	__in bool Fatal,
	__in unsigned long ErrorCode,
	__in void * Context
	)
{
	unsigned long PlayerId;

	PlayerId = (unsigned long) (ULONG_PTR) Context;

	//
	// Just log a warning that something is broken.  We could disconnect the
	// player here too, but synchronizing that with the rest of the internal
	// state would be painful.  We could also just write a disconnect packet
	// out to the player and then let the server's timeout handling eventually
	// drop the server's player state for that player.
	//

	wxLogMessage(wxT("OnNetLayerWindowStreamError: Stream error %lu for player %lu..."),
		PlayerId,
		ErrorCode);
	DebugPrint("OnNetLayerWindowStreamError: Stream error %lu for player %lu...\n",
		PlayerId,
		ErrorCode);

	return true;
}

unsigned long
__stdcall
SetGameObjUpdateSize2(
	__in unsigned long PlayerId
	)
{
	const unsigned long                  DefaultSize = 400;
	AURORA_SERVER_QUERY_SEND_QUEUE_DEPTH QueryDepth;

	if ((Connections[ PlayerId ] == NULL)     ||
	    (AuroraServerNetLayerQuery_ == NULL))
	{
		return DefaultSize;
	}

	//
	// Check the send queue depth.
	//

	if (!AuroraServerNetLayerQuery_(
		Connections[ PlayerId ],
		AuroraServerQuerySendQueueDepth,
		sizeof( QueryDepth ),
		NULL,
		&QueryDepth))
	{
		return DefaultSize;
	}

	//
	// Limit sends to default-sized send queues if this window is getting
	// behind, otherwise let it burst.
	//
	// As an exception, let the initial GameObjUpdate after joining an area
	// burst ahead even if the window wasn't empty -- which it would be
	// probably not, as we would have just sent a large chunk of data via the
	// Area.ClientArea message.
	//
	// This actually reduces the amount of data sent during area loading as it
	// allows a larger window of data to compress over !
	//

	if ((QueryDepth.SendQueueDepth >= 2) &&
	    (!PlayerState[ PlayerId ].AreaLoadPending))
		return DefaultSize;

	DebugPrint("Allowing burst GameObjUpdate transmission to %lu.\n", PlayerId);

	//
	// Otherwise, allow a large burst transmission to boost area loading
	// performance.
	//

	return (unsigned long) GameObjUpdateBurstSize;
}

__declspec(naked)
void
SetGameObjUpdateSize(
	)
{
	__asm
	{
		;
		; Calculate the actual update size to use.
		;

		push    ecx

		push    eax
		call    SetGameObjUpdateSize2

		pop     ecx

		;
		; Set the update size and return to normal program flow.
		;

		mov     esi, eax
		mov     eax, OFFS_GameObjUpdateSizeLimit1 + 6
		jmp     eax
	}
}
