/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#include "server.h"
#include "qcommon/stringed_ingame.h"
#include "server/sv_gameapi.h"
#include "qcommon/game_version.h"
#include "game/bg_weapons.h"
#include <thread>

//clientInfo_t* client = NULL;

#define SVTELL_PREFIX "\x19[Server^7\x19]\x19: "
#define SVSAY_PREFIX "Server^7\x19: "
#define SPAWN_VEHICLE_SUFFIX "(Spawns in 5 seconds)"

/*
===============================================================================
Move Client commands
===============================================================================
*/



void SV_ExecuteClientCommandDelayed_h(client_t* cl, char* cmd, int delay)
{
	std::this_thread::sleep_for(std::chrono::seconds(delay));
	Cvar_Set("sv_cheats", "1");
	GVM_RunFrame(sv.time);
	SV_ExecuteClientCommand(cl, cmd, qtrue);
	Cvar_Set("sv_cheats", "0");
	GVM_RunFrame(sv.time);
}

void SV_ClientTimedPowerup_h(client_t* cl, int pu, int duration)
{
	cl->gentity->playerState->powerups[pu] |= (1 << 21);
	std::this_thread::sleep_for(std::chrono::seconds(duration));
	cl->gentity->playerState->powerups[pu] = -1;
}

void SV_ExecuteClientCommandDelayed(client_t* cl, char* cmd, int delay)
{
	std::thread(SV_ExecuteClientCommandDelayed_h, cl, cmd, delay).detach();
}

void SV_ClientTimedPowerup(client_t* cl, int pu, int duration)
{
	std::thread(SV_ClientTimedPowerup_h, cl, pu, duration).detach();
}

/*
===============================================================================
MBII Specific Enums
===============================================================================
*/

typedef enum {
	MB_SS_NONE_0,
	MB_PW_FIRE,
	MB_PW_FREEZE,
	MB_SS_NONE_3,
	MB_SS_FLAG_RED,
	MB_SS_FLAG_BLUE,
	MB_SS_FLAG_WHITE,
	MB_SS_NONE_7,
	MB_SS_PHASING,
	MB_SS_NONE_9,
	MB_SS_NONE_10,
	MB_SS_CLOAKED,
	MB_PW_INVINSIBLE,
} powerups_mbii_t;


typedef enum {
	MB_SS_NONE,
	MB_SS_BLUE,
	MB_SS_YELLOW,
	MB_SS_RED,
	MB_SS_PURPLE,
	MB_SS_CYAN,
	MB_SS_DUEL,
	MB_SS_STAFF
} saber_style_mbii_t;

// MBII FORCE POWERS

typedef enum {
	MB_FORCE_HEAL,
	MB_FORCE_UNKNOWN_1,
	MB_FORCE_SPEED,
	MB_FORCE_PUSH,
	MB_FORCE_PULL,
	MB_FORCE_MIND_TRICK,
	MB_FORCE_GRIP,
	MB_FORCE_LIGHTNING,
	MB_FORCE_DESTRUCTION,
	MB_FORCE_PROTECT,
	MB_FORCE_ABSORB,
	MB_FORCE_TEAM_HEAL,
	MB_FORCE_TEAM_ENERGISE,
	MB_FORCE_DRAIN,
	MB_FORCE_SENSE
} force_mbii_t;

// MBII Classes
typedef enum {
	MB_CLASS_NONE,
	MB_CLASS_STORMTROOPER,
	MB_CLASS_SOLDER,
	MB_CLASS_COMMANDER,
	MB_CLASS_ELITE_SOLDER,
	MB_CLASS_SITH,
	MB_CLASS_JEDI,
	MB_CLASS_BOUNTY_HUNTER,
	MB_CLASS_HERO,
	MB_CLASS_SBD,
	MB_CLASS_WOOKIE,
	MB_CLASS_DEKA,
	MB_CLASS_CLONE,
	MB_CLASS_MANDO,
	MB_CLASS_ARC
} class_mbii_t;

// MBII Weapons
typedef enum {
	MB_NOTHING,
	MB_PROJECTILE_RIFLE,
	MB_MELEE,
	MB_LIGHTSABER,
	MB_PISTOL,
	MB_E11,
	MB_DISRUPTOR,
	MB_BOWCASTER,
	MB_DC15,
	MB_DC17_PISTOL,
	MB_DLT,
	MB_ROCKET_LAUNCHER,
	MB_FRAG_GREN,
	MB_PULSE_GREN,
	MB_T21,
	MB_ARM_BLASTER,
	MB_WESTAR34
} weapon_mbii_t;

// MBII AMMO
typedef enum {
	MB_AMMO_BOWCASTER_DISRUPTOR,
	MB_AMMO_DC15_DLT20_ARM_BLASTER,
	MB_AMMO_ROCKETS,
	MB_AMMO_UNKNOWN_1,
	MB_AMMO_FRAG_GRENADES,
	MB_AMMO_PULSE_GRENADES,
	MB_AMMO_T21_AMMO,
	MB_AMMO_PISTOL,
	MB_AMMO_WELSTAR34,
	MB_AMMO_DC17_PISTOL,
	MB_AMMO_PROJECTILE_RIFLE,
	MB_AMMO_UNKNOWN_4,
	MB_AMMO_UNKNOWN_5,
	MB_AMMO_UNKNOWN_6,
	MB_AMMO_UNKNOWN_7,
	MB_AMMO_UNKNOWN_8,
	MB_AMMO_UNKNOWN_9
} ammo_mbii_t;

/*
===============================================================================

OPERATOR CONSOLE ONLY COMMANDS

These commands can only be entered from stdin or by a remote operator datagram
===============================================================================
*/

const char *SV_GetStringEdString(char *refSection, char *refName)
{
	//Well, it would've been lovely doing it the above way, but it would mean mixing
	//languages for the client depending on what the server is. So we'll mark this as
	//a stringed reference with @@@ and send the refname to the client, and when it goes
	//to print it will get scanned for the stringed reference indication and dealt with
	//properly.
	static char text[1024]={0};
	Com_sprintf(text, sizeof(text), "@@@%s", refName);
	return text;
}

/*

==================
SV_GetPlayerByHandle

Returns the player with player id or name from Cmd_Argv(1)
==================
*/
static client_t *SV_GetPlayerByHandle( void ) {
	client_t	*cl;
	int			i;
	char		*s;
	char		cleanName[64];

	// make sure server is running
	if ( !com_sv_running->integer ) {
		return NULL;
	}

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "No player specified.\n" );
		return NULL;
	}

	s = Cmd_Argv(1);

	// Check whether this is a numeric player handle
	for(i = 0; s[i] >= '0' && s[i] <= '9'; i++);

	if(!s[i])
	{
		int plid = atoi(s);

		// Check for numeric playerid match
		if(plid >= 0 && plid < sv_maxclients->integer)
		{
			cl = &svs.clients[plid];

			if(cl->state)
				return cl;
		}
	}

	// check for a name match
	for ( i=0, cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++ ) {
		if ( !cl->state ) {
			continue;
		}
		if ( !Q_stricmp( cl->name, s ) ) {
			return cl;
		}

		Q_strncpyz( cleanName, cl->name, sizeof(cleanName) );
		Q_StripColor( cleanName );
		//Q_CleanStr( cleanName );
		if ( !Q_stricmp( cleanName, s ) ) {
			return cl;
		}
	}

	Com_Printf( "Player %s is not on the server\n", s );

	return NULL;
}

/*
==================
SV_GetPlayerByNum

Returns the player with idnum from Cmd_Argv(1)
==================
*/
static client_t *SV_GetPlayerByNum( void ) {
	client_t	*cl;
	int			i;
	int			idnum;
	char		*s;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		return NULL;
	}

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "No player specified.\n" );
		return NULL;
	}

	s = Cmd_Argv(1);

	for (i = 0; s[i]; i++) {
		if (s[i] < '0' || s[i] > '9') {
			Com_Printf( "Bad slot number: %s\n", s);
			return NULL;
		}
	}
	idnum = atoi( s );
	if ( idnum < 0 || idnum >= sv_maxclients->integer ) {
		Com_Printf( "Bad client slot: %i\n", idnum );
		return NULL;
	}

	cl = &svs.clients[idnum];
	if ( !cl->state ) {
		Com_Printf( "Client %i is not active\n", idnum );
		return NULL;
	}
	return cl;
}

//=========================================================

/*
==================
SV_Map_f

Restart the server on a different map
==================
*/
static void SV_Map_f( void ) {
	char		*cmd = NULL, *map = NULL;
	qboolean	killBots=qfalse, cheat=qfalse;
	char		expanded[MAX_QPATH] = {0}, mapname[MAX_QPATH] = {0};

	map = Cmd_Argv(1);
	if ( !map )
		return;

	// make sure the level exists before trying to change, so that
	// a typo at the server console won't end the game
	if (strchr (map, '\\') ) {
		Com_Printf ("Can't have mapnames with a \\\n");
		return;
	}

	Com_sprintf (expanded, sizeof(expanded), "maps/%s.bsp", map);
	if ( FS_ReadFile (expanded, NULL) == -1 ) {
		Com_Printf ("Can't find map %s\n", expanded);
		return;
	}

	// force latched values to get set
	Cvar_Get ("g_gametype", "0", CVAR_SERVERINFO | CVAR_LATCH );

	cmd = Cmd_Argv(0);
	if ( !Q_stricmpn( cmd, "devmap", 6 ) ) {
		cheat = qtrue;
		killBots = qtrue;
	} else {
		cheat = qfalse;
		killBots = qfalse;
	}

	// save the map name here cause on a map restart we reload the jampconfig.cfg
	// and thus nuke the arguments of the map command
	Q_strncpyz(mapname, map, sizeof(mapname));

	ForceReload_e eForceReload = eForceReload_NOTHING;	// default for normal load

//	if ( !Q_stricmp( cmd, "devmapbsp") ) {	// not relevant in MP codebase
//		eForceReload = eForceReload_BSP;
//	}
//	else
	if ( !Q_stricmp( cmd, "devmapmdl") ) {
		eForceReload = eForceReload_MODELS;
	}
	else
	if ( !Q_stricmp( cmd, "devmapall") ) {
		eForceReload = eForceReload_ALL;
	}

	// start up the map
	SV_SpawnServer( mapname, killBots, eForceReload );

	// set the cheat value
	// if the level was started with "map <levelname>", then
	// cheats will not be allowed.  If started with "devmap <levelname>"
	// then cheats will be allowed
	Cvar_Set( "sv_cheats", cheat ? "1" : "0" );
}


/*
================
SV_MapRestart_f

Completely restarts a level, but doesn't send a new gamestate to the clients.
This allows fair starts with variable load times.
================
*/
static void SV_MapRestart_f( void ) {
	int			i;
	client_t	*client;
	char		*denied;
	qboolean	isBot;
	int			delay;

	// make sure we aren't restarting twice in the same frame
	if ( com_frameTime == sv.serverId ) {
		return;
	}

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( sv.restartTime ) {
		return;
	}

	if (Cmd_Argc() > 1 ) {
		delay = atoi( Cmd_Argv(1) );
	}
	else {
		delay = 5;
	}
	if( delay ) {
		sv.restartTime = sv.time + delay * 1000;
		SV_SetConfigstring( CS_WARMUP, va("%i", sv.restartTime) );
		return;
	}

	// check for changes in variables that can't just be restarted
	// check for maxclients change
	if ( sv_maxclients->modified || sv_gametype->modified ) {
		char	mapname[MAX_QPATH];

		Com_Printf( "variable change -- restarting.\n" );
		// restart the map the slow way
		Q_strncpyz( mapname, Cvar_VariableString( "mapname" ), sizeof( mapname ) );

		SV_SpawnServer( mapname, qfalse, eForceReload_NOTHING );
		return;
	}

	SV_StopAutoRecordDemos();

	// toggle the server bit so clients can detect that a
	// map_restart has happened
	svs.snapFlagServerBit ^= SNAPFLAG_SERVERCOUNT;

	// generate a new serverid
	// TTimo - don't update restartedserverId there, otherwise we won't deal correctly with multiple map_restart
	sv.serverId = com_frameTime;
	Cvar_Set( "sv_serverid", va("%i", sv.serverId ) );

	time( &sv.realMapTimeStarted );
	sv.demosPruned = qfalse;

	// if a map_restart occurs while a client is changing maps, we need
	// to give them the correct time so that when they finish loading
	// they don't violate the backwards time check in cl_cgame.c
	for (i=0 ; i<sv_maxclients->integer ; i++) {
		if (svs.clients[i].state == CS_PRIMED) {
			svs.clients[i].oldServerTime = sv.restartTime;
		}
	}

	// reset all the vm data in place without changing memory allocation
	// note that we do NOT set sv.state = SS_LOADING, so configstrings that
	// had been changed from their default values will generate broadcast updates
	sv.state = SS_LOADING;
	sv.restarting = qtrue;

	SV_RestartGame();

	// run a few frames to allow everything to settle
	for ( i = 0 ;i < 3 ; i++ ) {
		GVM_RunFrame( sv.time );
		sv.time += 100;
		svs.time += 100;
	}

	sv.state = SS_GAME;
	sv.restarting = qfalse;

	// connect and begin all the clients
	for (i=0 ; i<sv_maxclients->integer ; i++) {
		client = &svs.clients[i];

		// send the new gamestate to all connected clients
		if ( client->state < CS_CONNECTED) {
			continue;
		}

		if ( client->netchan.remoteAddress.type == NA_BOT ) {
			isBot = qtrue;
		} else {
			isBot = qfalse;
		}

		// add the map_restart command
		SV_AddServerCommand( client, "map_restart\n" );

		// connect the client again, without the firstTime flag
		denied = GVM_ClientConnect( i, qfalse, isBot );
		if ( denied ) {
			// this generally shouldn't happen, because the client
			// was connected before the level change
			SV_DropClient( client, denied );
			Com_Printf( "SV_MapRestart_f(%d): dropped client %i - denied!\n", delay, i );
			continue;
		}

		if(client->state == CS_ACTIVE)
			SV_ClientEnterWorld(client, &client->lastUsercmd);
		else
		{
			// If we don't reset client->lastUsercmd and are restarting during map load,
			// the client will hang because we'll use the last Usercmd from the previous map,
			// which is wrong obviously.
			SV_ClientEnterWorld(client, NULL);
		}
	}

	// run another frame to allow things to look at all the players
	GVM_RunFrame( sv.time );
	sv.time += 100;
	svs.time += 100;

	SV_BeginAutoRecordDemos();
}

//===============================================================

/*
==================
SV_KickBlankPlayers
==================
*/
static void SV_KickBlankPlayers( void ) {
	client_t	*cl;
	int			i;
	char		cleanName[64];

	// make sure server is running
	if ( !com_sv_running->integer ) {
		return;
	}

	// check for a name match
	for ( i=0, cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++ ) {
		if ( !cl->state ) {
			continue;
		}
		if( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
			continue;
		}
		if ( !Q_stricmp( cl->name, "" ) ) {
			SV_DropClient( cl, SV_GetStringEdString("MP_SVGAME","WAS_KICKED"));	// "was kicked" );
			cl->lastPacketTime = svs.time;	// in case there is a funny zombie
			continue;
		}

		Q_strncpyz( cleanName, cl->name, sizeof(cleanName) );
		Q_StripColor( cleanName );
		//Q_CleanStr( cleanName );
		if ( !Q_stricmp( cleanName, "" ) ) {
			SV_DropClient( cl, SV_GetStringEdString("MP_SVGAME","WAS_KICKED"));	// "was kicked" );
			cl->lastPacketTime = svs.time;	// in case there is a funny zombie
		}
	}
}

/*
==================
SV_Kick_f

Kick a user off of the server
==================
*/
static void SV_Kick_f( void ) {
	client_t	*cl;
	int			i;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("Usage: kick <player name>\nkick all = kick everyone\nkick allbots = kick all bots\n");
		return;
	}

	if (!Q_stricmp(Cmd_Argv(1), "Padawan"))
	{ //if you try to kick the default name, also try to kick ""
		SV_KickBlankPlayers();
	}

	cl = SV_GetPlayerByHandle();
	if ( !cl ) {
		if ( !Q_stricmp(Cmd_Argv(1), "all") ) {
			for ( i=0, cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++ ) {
				if ( !cl->state ) {
					continue;
				}
				if( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
					continue;
				}
				SV_DropClient( cl, SV_GetStringEdString("MP_SVGAME","WAS_KICKED"));	// "was kicked" );
				cl->lastPacketTime = svs.time;	// in case there is a funny zombie
			}
		}
		else if ( !Q_stricmp(Cmd_Argv(1), "allbots") ) {
			for ( i=0, cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++ ) {
				if ( !cl->state ) {
					continue;
				}
				if( cl->netchan.remoteAddress.type != NA_BOT ) {
					continue;
				}
				SV_DropClient( cl, SV_GetStringEdString("MP_SVGAME","WAS_KICKED"));	// "was kicked" );
				cl->lastPacketTime = svs.time;	// in case there is a funny zombie
			}
		}
		return;
	}
	if( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
		Com_Printf("Cannot kick host player\n");
		return;
	}

	SV_DropClient( cl, SV_GetStringEdString("MP_SVGAME","WAS_KICKED"));	// "was kicked" );
	cl->lastPacketTime = svs.time;	// in case there is a funny zombie
}

/*
==================
SV_KickBots_f

Kick all bots off of the server
==================
*/
static void SV_KickBots_f( void ) {
	client_t	*cl;
	int			i;

	// make sure server is running
	if( !com_sv_running->integer ) {
		Com_Printf("Server is not running.\n");
		return;
	}

	for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
		if( !cl->state ) {
			continue;
		}

		if( cl->netchan.remoteAddress.type != NA_BOT ) {
			continue;
		}

		SV_DropClient( cl, SV_GetStringEdString("MP_SVGAME","WAS_KICKED"));	// "was kicked" );
		cl->lastPacketTime = svs.time; // in case there is a funny zombie
	}
}

/*
==================
SV_KickAll_f

Kick all users off of the server
==================
*/
static void SV_KickAll_f( void ) {
	client_t *cl;
	int i;

	// make sure server is running
	if( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ ) {
		if( !cl->state ) {
			continue;
		}

		if( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
			continue;
		}

		SV_DropClient( cl, SV_GetStringEdString("MP_SVGAME","WAS_KICKED"));	// "was kicked" );
		cl->lastPacketTime = svs.time; // in case there is a funny zombie
	}
}

/*
==================
SV_KickNum_f

Kick a user off of the server
==================
*/
static void SV_KickNum_f( void ) {
	client_t	*cl;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("Usage: %s <client number>\n", Cmd_Argv(0));
		return;
	}

	cl = SV_GetPlayerByNum();
	if ( !cl ) {
		return;
	}
	if( cl->netchan.remoteAddress.type == NA_LOOPBACK ) {
		Com_Printf("Cannot kick host player\n");
		return;
	}

	SV_DropClient( cl, SV_GetStringEdString("MP_SVGAME","WAS_KICKED"));	// "was kicked" );
	cl->lastPacketTime = svs.time;	// in case there is a funny zombie
}

/*
==================
SV_RehashBans_f

Load saved bans from file.
==================
*/
static void SV_RehashBans_f( void )
{
	int index, filelen;
	fileHandle_t readfrom;
	char *textbuf, *curpos, *maskpos, *newlinepos, *endpos;
	char filepath[MAX_QPATH];

	// make sure server is running
	if ( !com_sv_running->integer ) {
		return;
	}

	serverBansCount = 0;

	if ( !sv_banFile->string || !*sv_banFile->string )
		return;

	Com_sprintf( filepath, sizeof( filepath ), "%s/%s", FS_GetCurrentGameDir(), sv_banFile->string );

	if ( (filelen = FS_SV_FOpenFileRead( filepath, &readfrom )) >= 0 )
	{
		if ( filelen < 2 )
		{
			// Don't bother if file is too short.
			FS_FCloseFile( readfrom );
			return;
		}

		curpos = textbuf = (char *)Z_Malloc( filelen, TAG_TEMP_WORKSPACE );

		filelen = FS_Read( textbuf, filelen, readfrom );
		FS_FCloseFile( readfrom );

		endpos = textbuf + filelen;

		for ( index = 0; index < SERVER_MAXBANS && curpos + 2 < endpos; index++ )
		{
			// find the end of the address string
			for ( maskpos = curpos + 2; maskpos < endpos && *maskpos != ' '; maskpos++ );

			if ( maskpos + 1 >= endpos )
				break;

			*maskpos = '\0';
			maskpos++;

			// find the end of the subnet specifier
			for ( newlinepos = maskpos; newlinepos < endpos && *newlinepos != '\n'; newlinepos++ );

			if ( newlinepos >= endpos )
				break;

			*newlinepos = '\0';

			if ( NET_StringToAdr( curpos + 2, &serverBans[index].ip ) )
			{
				serverBans[index].isexception = (qboolean)(curpos[0] != '0');
				serverBans[index].subnet = atoi( maskpos );

				if ( serverBans[index].ip.type == NA_IP &&
					(serverBans[index].subnet < 1 || serverBans[index].subnet > 32) )
				{
					serverBans[index].subnet = 32;
				}
			}

			curpos = newlinepos + 1;
		}

		serverBansCount = index;

		Z_Free( textbuf );
	}
}

/*
==================
SV_WriteBans

Save bans to file.
==================
*/
static void SV_WriteBans( void )
{
	int index;
	fileHandle_t writeto;
	char filepath[MAX_QPATH];

	if ( !sv_banFile->string || !*sv_banFile->string )
		return;

	Com_sprintf( filepath, sizeof( filepath ), "%s/%s", FS_GetCurrentGameDir(), sv_banFile->string );

	if ( (writeto = FS_SV_FOpenFileWrite( filepath )) )
	{
		char writebuf[128];
		serverBan_t *curban;

		for ( index = 0; index < serverBansCount; index++ )
		{
			curban = &serverBans[index];

			Com_sprintf( writebuf, sizeof( writebuf ), "%d %s %d\n",
				curban->isexception, NET_AdrToString( curban->ip ), curban->subnet );
			FS_Write( writebuf, strlen( writebuf ), writeto );
		}

		FS_FCloseFile( writeto );
	}
}

/*
==================
SV_DelBanEntryFromList

Remove a ban or an exception from the list.
==================
*/

static qboolean SV_DelBanEntryFromList( int index ) {
	if ( index == serverBansCount - 1 )
		serverBansCount--;
	else if ( index < (int)ARRAY_LEN( serverBans ) - 1 )
	{
		memmove( serverBans + index, serverBans + index + 1, (serverBansCount - index - 1) * sizeof( *serverBans ) );
		serverBansCount--;
	}
	else
		return qtrue;

	return qfalse;
}

/*
==================
SV_ParseCIDRNotation

Parse a CIDR notation type string and return a netadr_t and suffix by reference
==================
*/

static qboolean SV_ParseCIDRNotation( netadr_t *dest, int *mask, char *adrstr )
{
	char *suffix;

	suffix = strchr( adrstr, '/' );
	if ( suffix )
	{
		*suffix = '\0';
		suffix++;
	}

	if ( !NET_StringToAdr( adrstr, dest ) )
		return qtrue;

	if ( suffix )
	{
		*mask = atoi( suffix );

		if ( dest->type == NA_IP )
		{
			if ( *mask < 1 || *mask > 32 )
				*mask = 32;
		}
		else
			*mask = 32;
	}
	//else if ( dest->type == NA_IP )
	//	*mask = 32;
	else
		*mask = 32;

	return qfalse;
}

/*
==================
SV_AddBanToList

Ban a user from being able to play on this server based on his ip address.
==================
*/

static void SV_AddBanToList( qboolean isexception )
{
	char *banstring;
	char addy2[NET_ADDRSTRMAXLEN];
	netadr_t ip;
	int index, argc, mask;
	serverBan_t *curban;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	argc = Cmd_Argc();

	if ( argc < 2 || argc > 3 )
	{
		Com_Printf( "Usage: %s (ip[/subnet] | clientnum [subnet])\n", Cmd_Argv( 0 ) );
		return;
	}

	if ( serverBansCount >= (int)ARRAY_LEN( serverBans ) )
	{
		Com_Printf( "Error: Maximum number of bans/exceptions exceeded.\n" );
		return;
	}

	banstring = Cmd_Argv( 1 );

	if ( strchr( banstring, '.' ) /*|| strchr( banstring, ':' )*/ )
	{
		// This is an ip address, not a client num.

		if ( SV_ParseCIDRNotation( &ip, &mask, banstring ) )
		{
			Com_Printf( "Error: Invalid address %s\n", banstring );
			return;
		}
	}
	else
	{
		client_t *cl;

		// client num.

		cl = SV_GetPlayerByNum();

		if ( !cl )
		{
			Com_Printf( "Error: Playernum %s does not exist.\n", Cmd_Argv( 1 ) );
			return;
		}

		ip = cl->netchan.remoteAddress;

		if ( argc == 3 )
		{
			mask = atoi( Cmd_Argv( 2 ) );

			if ( ip.type == NA_IP )
			{
				if ( mask < 1 || mask > 32 )
					mask = 32;
			}
			else
				mask = 32;
		}
		else
			mask = 32;
	}

	if ( ip.type != NA_IP )
	{
		Com_Printf( "Error: Can ban players connected via the internet only.\n" );
		return;
	}

	// first check whether a conflicting ban exists that would supersede the new one.
	for ( index = 0; index < serverBansCount; index++ )
	{
		curban = &serverBans[index];

		if ( curban->subnet <= mask )
		{
			if ( (curban->isexception || !isexception) && NET_CompareBaseAdrMask( curban->ip, ip, curban->subnet ) )
			{
				Q_strncpyz( addy2, NET_AdrToString( ip ), sizeof( addy2 ) );

				Com_Printf( "Error: %s %s/%d supersedes %s %s/%d\n", curban->isexception ? "Exception" : "Ban",
					NET_AdrToString( curban->ip ), curban->subnet,
					isexception ? "exception" : "ban", addy2, mask );
				return;
			}
		}
		if ( curban->subnet >= mask )
		{
			if ( !curban->isexception && isexception && NET_CompareBaseAdrMask( curban->ip, ip, mask ) )
			{
				Q_strncpyz( addy2, NET_AdrToString( curban->ip ), sizeof( addy2 ) );

				Com_Printf( "Error: %s %s/%d supersedes already existing %s %s/%d\n", isexception ? "Exception" : "Ban",
					NET_AdrToString( ip ), mask,
					curban->isexception ? "exception" : "ban", addy2, curban->subnet );
				return;
			}
		}
	}

	// now delete bans that are superseded by the new one
	index = 0;
	while ( index < serverBansCount )
	{
		curban = &serverBans[index];

		if ( curban->subnet > mask && (!curban->isexception || isexception) && NET_CompareBaseAdrMask( curban->ip, ip, mask ) )
			SV_DelBanEntryFromList( index );
		else
			index++;
	}

	serverBans[serverBansCount].ip = ip;
	serverBans[serverBansCount].subnet = mask;
	serverBans[serverBansCount].isexception = isexception;

	serverBansCount++;

	SV_WriteBans();

	Com_Printf( "Added %s: %s/%d\n", isexception ? "ban exception" : "ban",
		NET_AdrToString( ip ), mask );
}

/*
==================
SV_DelBanFromList

Remove a ban or an exception from the list.
==================
*/

static void SV_DelBanFromList( qboolean isexception )
{
	int index, count = 0, todel, mask;
	netadr_t ip;
	char *banstring;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 )
	{
		Com_Printf( "Usage: %s (ip[/subnet] | num)\n", Cmd_Argv( 0 ) );
		return;
	}

	banstring = Cmd_Argv( 1 );

	if ( strchr( banstring, '.' ) || strchr( banstring, ':' ) )
	{
		serverBan_t *curban;

		if ( SV_ParseCIDRNotation( &ip, &mask, banstring ) )
		{
			Com_Printf( "Error: Invalid address %s\n", banstring );
			return;
		}

		index = 0;

		while ( index < serverBansCount )
		{
			curban = &serverBans[index];

			if ( curban->isexception == isexception		&&
				curban->subnet >= mask 			&&
				NET_CompareBaseAdrMask( curban->ip, ip, mask ) )
			{
				Com_Printf( "Deleting %s %s/%d\n",
					isexception ? "exception" : "ban",
					NET_AdrToString( curban->ip ), curban->subnet );

				SV_DelBanEntryFromList( index );
			}
			else
				index++;
		}
	}
	else
	{
		todel = atoi( Cmd_Argv( 1 ) );

		if ( todel < 1 || todel > serverBansCount )
		{
			Com_Printf( "Error: Invalid ban number given\n" );
			return;
		}

		for ( index = 0; index < serverBansCount; index++ )
		{
			if ( serverBans[index].isexception == isexception )
			{
				count++;

				if ( count == todel )
				{
					Com_Printf( "Deleting %s %s/%d\n",
						isexception ? "exception" : "ban",
						NET_AdrToString( serverBans[index].ip ), serverBans[index].subnet );

					SV_DelBanEntryFromList( index );

					break;
				}
			}
		}
	}

	SV_WriteBans();
}


/*
==================
SV_ListBans_f

List all bans and exceptions on console
==================
*/

static void SV_ListBans_f( void )
{
	int index, count;
	serverBan_t *ban;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	// List all bans
	for ( index = count = 0; index < serverBansCount; index++ )
	{
		ban = &serverBans[index];
		if ( !ban->isexception )
		{
			count++;

			Com_Printf( "Ban #%d: %s/%d\n", count,
				NET_AdrToString( ban->ip ), ban->subnet );
		}
	}
	// List all exceptions
	for ( index = count = 0; index < serverBansCount; index++ )
	{
		ban = &serverBans[index];
		if ( ban->isexception )
		{
			count++;

			Com_Printf( "Except #%d: %s/%d\n", count,
				NET_AdrToString( ban->ip ), ban->subnet );
		}
	}
}

/*
==================
SV_FlushBans_f

Delete all bans and exceptions.
==================
*/

static void SV_FlushBans_f( void )
{
	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	serverBansCount = 0;

	// empty the ban file.
	SV_WriteBans();

	Com_Printf( "All bans and exceptions have been deleted.\n" );
}

static void SV_BanAddr_f( void )
{
	SV_AddBanToList( qfalse );
}

static void SV_ExceptAddr_f( void )
{
	SV_AddBanToList( qtrue );
}

static void SV_BanDel_f( void )
{
	SV_DelBanFromList( qfalse );
}

static void SV_ExceptDel_f( void )
{
	SV_DelBanFromList( qtrue );
}

static const char *SV_CalcUptime( void ) {
	static char buf[MAX_STRING_CHARS / 4] = { '\0' };
	char tmp[64] = { '\0' };
	time_t currTime;

	time( &currTime );

	int secs = difftime( currTime, svs.startTime );
	int mins = secs / 60;
	int hours = mins / 60;
	int days = hours / 24;

	secs %= 60;
	mins %= 60;
	hours %= 24;
	//days %= 365;

	buf[0] = '\0';
	if ( days > 0 ) {
		Com_sprintf( tmp, sizeof(tmp), "%i days ", days );
		Q_strcat( buf, sizeof(buf), tmp );
	}

	Com_sprintf( tmp, sizeof(tmp), "%ih%im%is", hours, mins, secs );
	Q_strcat( buf, sizeof(buf), tmp );

	return buf;
}

/*
================
SV_Status_f
================
*/
static void SV_Status_f( void )
{
	int				i, humans, bots;
	client_t		*cl;
	playerState_t	*ps;
	const char		*s;
	int				ping;
	char			state[32];
	qboolean		avoidTruncation = qfalse;

	// make sure server is running
	if ( !com_sv_running->integer )
	{
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() > 1 )
	{
		if (!Q_stricmp("notrunc", Cmd_Argv(1)))
		{
			avoidTruncation = qtrue;
		}
	}

	humans = bots = 0;
	for ( i = 0 ; i < sv_maxclients->integer ; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED ) {
			if ( svs.clients[i].netchan.remoteAddress.type != NA_BOT ) {
				humans++;
			}
			else {
				bots++;
			}
		}
	}

#if defined(_WIN32)
#define STATUS_OS "Windows"
#elif defined(__linux__)
#define STATUS_OS "Linux"
#elif defined(MACOS_X)
#define STATUS_OS "OSX"
#else
#define STATUS_OS "Unknown"
#endif

	const char *ded_table[] =
	{
		"listen",
		"lan dedicated",
		"public dedicated",
	};

	char hostname[MAX_HOSTNAMELENGTH] = { 0 };

	Q_strncpyz( hostname, sv_hostname->string, sizeof(hostname) );
	Q_StripColor( hostname );

	Com_Printf( "hostname: %s^7\n", hostname );
	Com_Printf( "version : %s %i\n", VERSION_STRING_DOTTED, PROTOCOL_VERSION );
	Com_Printf( "game    : %s\n", FS_GetCurrentGameDir() );
	Com_Printf( "udp/ip  : %s:%i os(%s) type(%s)\n", Cvar_VariableString( "net_ip" ), Cvar_VariableIntegerValue( "net_port" ), STATUS_OS, ded_table[com_dedicated->integer] );
	Com_Printf( "map     : %s gametype(%i)\n", sv_mapname->string, sv_gametype->integer );
	Com_Printf( "players : %i humans, %i bots (%i max)\n", humans, bots, sv_maxclients->integer - sv_privateClients->integer );
	Com_Printf( "uptime  : %s\n", SV_CalcUptime() );

	Com_Printf ("cl score ping name            address                                 rate \n");
	Com_Printf ("-- ----- ---- --------------- --------------------------------------- -----\n");
	for (i=0,cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++)
	{
		if ( !cl->state )
			continue;

		if ( cl->state == CS_CONNECTED )
			Q_strncpyz( state, "CON ", sizeof( state ) );
		else if ( cl->state == CS_ZOMBIE )
			Q_strncpyz( state, "ZMB ", sizeof( state ) );
		else {
			ping = cl->ping < 9999 ? cl->ping : 9999;
			Com_sprintf( state, sizeof(state), "%4i", ping );
		}

		ps = SV_GameClientNum( i );
		s = NET_AdrToString( cl->netchan.remoteAddress );

		if (!avoidTruncation)
		{
			Com_Printf ("%2i %5i %s %-15.15s ^7%39s %5i\n",
				i,
				ps->persistant[PERS_SCORE],
				state,
				cl->name,
				s,
				cl->rate
				);
		}
		else
		{
			Com_Printf ("%2i %5i %s %s ^7%39s %5i\n",
				i,
				ps->persistant[PERS_SCORE],
				state,
				cl->name,
				s,
				cl->rate
				);
		}
	}
	Com_Printf ("\n");
}

char	*SV_ExpandNewlines( char *in );


/*
==================
SV_ConSay_f
==================
*/
static void SV_ConSay_f(void) {
	char	text[MAX_SAY_TEXT] = {0};

	if( !com_dedicated->integer ) {
		Com_Printf( "Server is not dedicated.\n" );
		return;
	}

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc () < 2 ) {
		return;
	}

	Cmd_ArgsBuffer( text, sizeof(text) );

	Com_Printf ("broadcast: chat \"" SVSAY_PREFIX "%s\\n\"\n", SV_ExpandNewlines((char *)text) );
	SV_SendServerCommand(NULL, "chat \"" SVSAY_PREFIX "%s\"\n", text);
}


/*
==================
SV_ConTell_f
==================
*/
static void SV_ConTell_f(void) {
	char	text[MAX_SAY_TEXT] = {0};
	client_t	*cl;

	if( !com_dedicated->integer ) {
		Com_Printf( "Server is not dedicated.\n" );
		return;
	}

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc () < 3 ) {
		Com_Printf ("Usage: svtell <client number> <text>\n");
		return;
	}

	cl = SV_GetPlayerByNum();
	if ( !cl ) {
		return;
	}

	Cmd_ArgsFromBuffer( 2, text, sizeof(text) );

	Com_Printf ("tell: svtell to %s" S_COLOR_WHITE ": %s\n", cl->name, SV_ExpandNewlines((char *)text) );
	SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_MAGENTA "%s" S_COLOR_WHITE "\"\n", text);
}

const char *forceToggleNamePrints[NUM_FORCE_POWERS] = {
	"HEAL",
	"JUMP",
	"SPEED",
	"PUSH",
	"PULL",
	"MINDTRICK",
	"GRIP",
	"LIGHTNING",
	"DARK RAGE",
	"PROTECT",
	"ABSORB",
	"TEAM HEAL",
	"TEAM REPLENISH",
	"DRAIN",
	"SEEING",
	"SABER OFFENSE",
	"SABER DEFENSE",
	"SABER THROW",
};

static void SV_ForceToggle_f( void ) {
	int bits = Cvar_VariableIntegerValue("g_forcePowerDisable");
	int i, val;
	char *s;

	// make sure server is running
	if( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 ) {
		for ( i = 0; i<NUM_FORCE_POWERS; i++ ) {
			if ( (bits & (1 << i)) )		Com_Printf( "%2d [X] %s\n", i, forceToggleNamePrints[i] );
			else							Com_Printf( "%2d [ ] %s\n", i, forceToggleNamePrints[i] );
		}
		Com_Printf( "Example usage: forcetoggle 3(toggles PUSH)\n" );
		return;
	}

	s = Cmd_Argv(1);

	if( Q_isanumber( s ) ) {
		val = atoi(s);
		if( val >= 0 && val < NUM_FORCE_POWERS) {
			bits ^= (1 << val);
			Cvar_SetValue("g_forcePowerDisable", bits);
			Com_Printf( "%s %s^7\n", forceToggleNamePrints[val], (bits & (1<<val)) ? "^2Enabled" : "^1Disabled" );
		}
		else {
			for ( i = 0; i<NUM_FORCE_POWERS; i++ ) {
				if ( (bits & (1 << i)) )		Com_Printf( "%2d [X] %s\n", i, forceToggleNamePrints[i] );
				else							Com_Printf( "%2d [ ] %s\n", i, forceToggleNamePrints[i] );
			}
			Com_Printf ("Specified a power that does not exist.\nExample usage: forcetoggle 3\n(toggles PUSH)\n");
		}
	}
	else {
		for ( i = 0; i<NUM_FORCE_POWERS; i++ ) {
			if ( (bits & (1 << i)) )		Com_Printf( "%2d [X] %s\n", i, forceToggleNamePrints[i] );
			else							Com_Printf( "%2d [ ] %s\n", i, forceToggleNamePrints[i] );
		}
		Com_Printf ("Specified a power that does not exist.\nExample usage: forcetoggle 3\n(toggles PUSH)\n");
	}
}

const char *weaponToggleNamePrints[WP_NUM_WEAPONS] = {
	"NO WEAPON",
	"STUN BATON",
	"MELEE",
	"SABER",
	"BRYAR PISTOL",
	"BLASTER",
	"DISRUPTOR",
	"BOWCASTER",
	"REPEATER",
	"DEMP2",
	"FLECHETTE",
	"ROCKET LAUNCHER",
	"THERMAL",
	"TRIP MINE",
	"DET PACK",
	"CONCUSSION",
	"BRYAR OLD",
	"EMPLACED GUN",
	"TURRET"
};

static void SV_WeaponToggle_f( void ) {
	int bits = 0;
	int i, val;
	char *s;
	const char *cvarStr = NULL;

	if ( sv_gametype->integer == GT_DUEL || sv_gametype->integer == GT_POWERDUEL ) {
		cvarStr = "g_duelWeaponDisable";
		bits = Cvar_VariableIntegerValue( "g_duelWeaponDisable" );
	}
	else {
		cvarStr = "g_weaponDisable";
		bits = Cvar_VariableIntegerValue( "g_weaponDisable" );
	}

	// make sure server is running
	if( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 ) {
		for ( i = 0; i<WP_NUM_WEAPONS; i++ ) {
			if ( (bits & (1 << i)) )		Com_Printf( "%2d [X] %s\n", i, weaponToggleNamePrints[i] );
			else							Com_Printf( "%2d [ ] %s\n", i, weaponToggleNamePrints[i] );
		}
		Com_Printf ("Example usage: weapontoggle 3(toggles SABER)\n");
		return;
	}

	s = Cmd_Argv(1);

	if( Q_isanumber( s ) ) {
		val = atoi(s);
		if( val >= 0 && val < WP_NUM_WEAPONS) {
			bits ^= (1 << val);
			Cvar_SetValue(cvarStr, bits);
			Com_Printf( "%s %s^7\n", weaponToggleNamePrints[val], (bits & (1 << val)) ? "^2Enabled" : "^1Disabled" );
		}
		else {
			for ( i = 0; i<WP_NUM_WEAPONS; i++ ) {
				if ( (bits & (1 << i)) )		Com_Printf( "%2d [X] %s\n", i, weaponToggleNamePrints[i] );
				else							Com_Printf( "%2d [ ] %s\n", i, weaponToggleNamePrints[i] );
			}
			Com_Printf ("Specified a weapon that does not exist.\nExample usage: weapontoggle 3\n(toggles SABER)\n");
		}
	}
	else {
		for ( i = 0; i<WP_NUM_WEAPONS; i++ ) {
			if ( (bits & (1 << i)) )		Com_Printf( "%2d [X] %s\n", i, weaponToggleNamePrints[i] );
			else							Com_Printf( "%2d [ ] %s\n", i, weaponToggleNamePrints[i] );
		}
		Com_Printf ("Specified a weapon that does not exist.\nExample usage: weapontoggle 3\n(toggles SABER)\n");
	}
}

/*
==================
SV_Heartbeat_f

Also called by SV_DropClient, SV_DirectConnect, and SV_SpawnServer
==================
*/
void SV_Heartbeat_f( void ) {
	svs.nextHeartbeatTime = -9999999;
}

/*
===========
SV_Serverinfo_f

Examine the serverinfo string
===========
*/
static void SV_Serverinfo_f( void ) {
	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	Com_Printf ("Server info settings:\n");
	Info_Print ( Cvar_InfoString( CVAR_SERVERINFO ) );
}

/*
===========
SV_Systeminfo_f

Examine or change the serverinfo string
===========
*/
static void SV_Systeminfo_f( void ) {
	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	Com_Printf ("System info settings:\n");
	Info_Print ( Cvar_InfoString_Big( CVAR_SYSTEMINFO ) );
}

/*
===========
SV_DumpUser_f

Examine all a users info strings FIXME: move to game
===========
*/
static void SV_DumpUser_f( void ) {
	client_t	*cl;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("Usage: dumpuser <userid>\n");
		return;
	}

	cl = SV_GetPlayerByHandle();
	if ( !cl ) {
		return;
	}

	Com_Printf( "userinfo\n" );
	Com_Printf( "--------\n" );
	Info_Print( cl->userinfo );
}

/*
=================
SV_KillServer
=================
*/
static void SV_KillServer_f( void ) {
	SV_Shutdown( "killserver" );
}

void SV_WriteDemoMessage ( client_t *cl, msg_t *msg, int headerBytes ) {
	int		len, swlen;

	// write the packet sequence
	len = cl->netchan.outgoingSequence;
	swlen = LittleLong( len );
	FS_Write( &swlen, 4, cl->demo.demofile );

	// skip the packet sequencing information
	len = msg->cursize - headerBytes;
	swlen = LittleLong( len );
	FS_Write( &swlen, 4, cl->demo.demofile );
	FS_Write( msg->data + headerBytes, len, cl->demo.demofile );
}

void SV_StopRecordDemo( client_t *cl ) {
	int		len;

	if ( !cl->demo.demorecording ) {
		Com_Printf( "Client %d is not recording a demo.\n", cl - svs.clients );
		return;
	}

	// finish up
	len = -1;
	FS_Write (&len, 4, cl->demo.demofile);
	FS_Write (&len, 4, cl->demo.demofile);
	FS_FCloseFile (cl->demo.demofile);
	cl->demo.demofile = 0;
	cl->demo.demorecording = qfalse;
	Com_Printf ("Stopped demo for client %d.\n", cl - svs.clients);
}

// stops all recording demos
void SV_StopAutoRecordDemos() {
	if ( svs.clients && sv_autoDemo->integer ) {
		for ( client_t *client = svs.clients; client - svs.clients < sv_maxclients->integer; client++ ) {
			if ( client->demo.demorecording) {
				SV_StopRecordDemo( client );
			}
		}
	}
}

/*
====================
SV_StopRecording_f

stop recording a demo
====================
*/
void SV_StopRecord_f( void ) {
	int		i;

	client_t *cl = NULL;
	if ( Cmd_Argc() == 2 ) {
		int clIndex = atoi( Cmd_Argv( 1 ) );
		if ( clIndex < 0 || clIndex >= sv_maxclients->integer ) {
			Com_Printf( "Unknown client number %d.\n", clIndex );
			return;
		}
		cl = &svs.clients[clIndex];
	} else {
		for (i = 0; i < sv_maxclients->integer; i++) {
			if ( svs.clients[i].demo.demorecording ) {
				cl = &svs.clients[i];
				break;
			}
		}
		if ( cl == NULL ) {
			Com_Printf( "No demo being recorded.\n" );
			return;
		}
	}
	SV_StopRecordDemo( cl );
}

/*
==================
SV_DemoFilename
==================
*/
void SV_DemoFilename( char *buf, int bufSize ) {
	time_t rawtime;
	char timeStr[32] = {0}; // should really only reach ~19 chars

	time( &rawtime );
	strftime( timeStr, sizeof( timeStr ), "%Y-%m-%d_%H-%M-%S", localtime( &rawtime ) ); // or gmtime

	Com_sprintf( buf, bufSize, "demo%s", timeStr );
}

// defined in sv_client.cpp
extern void SV_CreateClientGameStateMessage( client_t *client, msg_t* msg );

void SV_RecordDemo( client_t *cl, char *demoName ) {
	char		name[MAX_OSPATH];
	byte		bufData[MAX_MSGLEN];
	msg_t		msg;
	int			len;

	if ( cl->demo.demorecording ) {
		Com_Printf( "Already recording.\n" );
		return;
	}

	if ( cl->state != CS_ACTIVE ) {
		Com_Printf( "Client is not active.\n" );
		return;
	}

	// open the demo file
	Q_strncpyz( cl->demo.demoName, demoName, sizeof( cl->demo.demoName ) );
	Com_sprintf( name, sizeof( name ), "demos/%s.dm_%d", cl->demo.demoName, PROTOCOL_VERSION );
	Com_Printf( "recording to %s.\n", name );
	cl->demo.demofile = FS_FOpenFileWrite( name );
	if ( !cl->demo.demofile ) {
		Com_Printf ("ERROR: couldn't open.\n");
		return;
	}
	cl->demo.demorecording = qtrue;

	// don't start saving messages until a non-delta compressed message is received
	cl->demo.demowaiting = qtrue;

	cl->demo.isBot = ( cl->netchan.remoteAddress.type == NA_BOT ) ? qtrue : qfalse;
	cl->demo.botReliableAcknowledge = cl->reliableSent;

	// write out the gamestate message
	MSG_Init( &msg, bufData, sizeof( bufData ) );

	// NOTE, MRE: all server->client messages now acknowledge
	int tmp = cl->reliableSent;
	SV_CreateClientGameStateMessage( cl, &msg );
	cl->reliableSent = tmp;

	// finished writing the client packet
	MSG_WriteByte( &msg, svc_EOF );

	// write it to the demo file
	len = LittleLong( cl->netchan.outgoingSequence - 1 );
	FS_Write( &len, 4, cl->demo.demofile );

	len = LittleLong( msg.cursize );
	FS_Write( &len, 4, cl->demo.demofile );
	FS_Write( msg.data, msg.cursize, cl->demo.demofile );

	// the rest of the demo file will be copied from net messages
}

void SV_AutoRecordDemo( client_t *cl ) {
	char demoName[MAX_OSPATH];
	char demoFolderName[MAX_OSPATH];
	char demoFileName[MAX_OSPATH];
	char *demoNames[] = { demoFolderName, demoFileName };
	char date[MAX_OSPATH];
	char folderDate[MAX_OSPATH];
	char folderTreeDate[MAX_OSPATH];
	char demoPlayerName[MAX_NAME_LENGTH];
	time_t rawtime;
	struct tm * timeinfo;
	time( &rawtime );
	timeinfo = localtime( &rawtime );
	strftime( date, sizeof( date ), "%Y-%m-%d_%H-%M-%S", timeinfo );
	timeinfo = localtime( &sv.realMapTimeStarted );
	strftime( folderDate, sizeof( folderDate ), "%Y-%m-%d_%H-%M-%S", timeinfo );
	strftime( folderTreeDate, sizeof( folderTreeDate ), "%Y/%m/%d", timeinfo );
	Q_strncpyz( demoPlayerName, cl->name, sizeof( demoPlayerName ) );
	Q_CleanStr( demoPlayerName );
	Com_sprintf( demoFileName, sizeof( demoFileName ), "%d %s %s %s",
			cl - svs.clients, demoPlayerName, Cvar_VariableString( "mapname" ), date );
	Com_sprintf( demoFolderName, sizeof( demoFolderName ), "%s %s", Cvar_VariableString( "mapname" ), folderDate );
	// sanitize filename
	for ( char **start = demoNames; start - demoNames < (ptrdiff_t)ARRAY_LEN( demoNames ); start++ ) {
		Q_strstrip( *start, "\n\r;:.?*<>|\\/\"", NULL );
	}
	Com_sprintf( demoName, sizeof( demoName ), "autorecord/%s/%s/%s", folderTreeDate, demoFolderName, demoFileName );
	SV_RecordDemo( cl, demoName );
}

static time_t SV_ExtractTimeFromDemoFolder( char *folder ) {
	char *slash = strrchr( folder, '/' );
	if ( slash ) {
		folder = slash + 1;
	}
	size_t timeLen = strlen( "0000-00-00_00-00-00" );
	if ( strlen( folder ) < timeLen ) {
		return 0;
	}
	struct tm timeinfo;
	timeinfo.tm_isdst = 0;
	int numMatched = sscanf( folder + ( strlen(folder) - timeLen ), "%4d-%2d-%2d_%2d-%2d-%2d",
		&timeinfo.tm_year, &timeinfo.tm_mon, &timeinfo.tm_mday, &timeinfo.tm_hour, &timeinfo.tm_min, &timeinfo.tm_sec);
	if ( numMatched < 6 ) {
		// parsing failed
		return 0;
	}
	timeinfo.tm_year -= 1900;
	timeinfo.tm_mon--;
	return mktime( &timeinfo );
}

static int QDECL SV_DemoFolderTimeComparator( const void *arg1, const void *arg2 ) {
	char *left = (char *)arg1, *right = (char *)arg2;
	time_t leftTime = SV_ExtractTimeFromDemoFolder( left );
	time_t rightTime = SV_ExtractTimeFromDemoFolder( right );
	if ( leftTime == 0 && rightTime == 0 ) {
		return -strcmp( left, right );
	} else if ( leftTime == 0 ) {
		return 1;
	} else if ( rightTime == 0 ) {
		return -1;
	}
	return rightTime - leftTime;
}

// returns number of folders found.  pass NULL result pointer for just a count.
static int SV_FindLeafFolders( const char *baseFolder, char *result, int maxResults, int maxFolderLength ) {
	char *fileList = (char *)Z_Malloc( MAX_OSPATH * maxResults, TAG_FILESYS ); // too big for stack since this is recursive
	char fullFolder[MAX_OSPATH];
	int resultCount = 0;
	char *fileName;
	int i;
	int numFiles = FS_GetFileList( baseFolder, "/", fileList, MAX_OSPATH * maxResults );

	fileName = fileList;
	for ( i = 0; i < numFiles; i++ ) {
		if ( Q_stricmp( fileName, "." ) && Q_stricmp( fileName, ".." ) ) {
			char *nextResult = NULL;
			Com_sprintf( fullFolder, sizeof( fullFolder ), "%s/%s", baseFolder, fileName );
			if ( result != NULL ) {
				nextResult = &result[maxFolderLength * resultCount];
			}
			int newResults = SV_FindLeafFolders( fullFolder, nextResult, maxResults - resultCount, maxFolderLength );
			resultCount += newResults;
			if ( result != NULL && resultCount >= maxResults ) {
				break;
			}
			if ( newResults == 0 ) {
				if ( result != NULL ) {
					Q_strncpyz( &result[maxFolderLength * resultCount], fullFolder, maxFolderLength );
				}
				resultCount++;
				if ( result != NULL && resultCount >= maxResults ) {
					break;
				}
			}
		}
		fileName += strlen( fileName ) + 1;
	}

	Z_Free( fileList );

	return resultCount;
}

// starts demo recording on all active clients
void SV_BeginAutoRecordDemos() {
	if ( sv_autoDemo->integer ) {
		for ( client_t *client = svs.clients; client - svs.clients < sv_maxclients->integer; client++ ) {
			if ( client->state == CS_ACTIVE && !client->demo.demorecording ) {
				if ( client->netchan.remoteAddress.type != NA_BOT || sv_autoDemoBots->integer ) {
					SV_AutoRecordDemo( client );
				}
			}
		}
		if ( sv_autoDemoMaxMaps->integer > 0 && sv.demosPruned == qfalse ) {
			char autorecordDirList[500 * MAX_OSPATH], tmpFileList[5 * MAX_OSPATH];
			int autorecordDirListCount = SV_FindLeafFolders( "demos/autorecord", autorecordDirList, 500, MAX_OSPATH );
			int i;

			qsort( autorecordDirList, autorecordDirListCount, MAX_OSPATH, SV_DemoFolderTimeComparator );
			for ( i = sv_autoDemoMaxMaps->integer; i < autorecordDirListCount; i++ ) {
				char *folder = &autorecordDirList[i * MAX_OSPATH], *slash = NULL;
				FS_HomeRmdir( folder, qtrue );
				// if this folder was the last thing in its parent folder (and its parent isn't the root folder),
				// also delete the parent.
				for (;;) {
					slash = strrchr( folder, '/' );
					if ( slash == NULL ) {
						break;
					}
					slash[0] = '\0';
					if ( !strcmp( folder, "demos/autorecord" ) ) {
						break;
					}
					int numFiles = FS_GetFileList( folder, "", tmpFileList, sizeof( tmpFileList ) );
					int numFolders = FS_GetFileList( folder, "/", tmpFileList, sizeof( tmpFileList ) );
					// numFolders will include . and ..
					if ( numFiles == 0 && numFolders == 2 ) {
						// dangling empty folder, delete
						FS_HomeRmdir( folder, qfalse );
					} else {
						break;
					}
				}
			}
			sv.demosPruned = qtrue;
		}
	}
}

// code is a merge of the cl_main.cpp function of the same name and SV_SendClientGameState in sv_client.cpp
static void SV_Record_f( void ) {
	char		demoName[MAX_OSPATH];
	char		name[MAX_OSPATH];
	int			i;
	char		*s;
	client_t	*cl;

	if ( svs.clients == NULL ) {
		Com_Printf( "cannot record server demo - null svs.clients\n" );
		return;
	}

	if ( Cmd_Argc() > 3 ) {
		Com_Printf( "record <demoname> <clientnum>\n" );
		return;
	}


	if ( Cmd_Argc() == 3 ) {
		int clIndex = atoi( Cmd_Argv( 2 ) );
		if ( clIndex < 0 || clIndex >= sv_maxclients->integer ) {
			Com_Printf( "Unknown client number %d.\n", clIndex );
			return;
		}
		cl = &svs.clients[clIndex];
	} else {
		for ( i=0, cl=svs.clients ; i < sv_maxclients->integer ; i++, cl++ )
		{
			if ( !cl->state )
			{
				continue;
			}

			if ( cl->demo.demorecording )
			{
				continue;
			}

			if ( cl->state == CS_ACTIVE )
			{
				break;
			}
		}
	}

	if (cl - svs.clients >= sv_maxclients->integer) {
		Com_Printf( "No active client could be found.\n" );
		return;
	}

	if ( cl->demo.demorecording ) {
		Com_Printf( "Already recording.\n" );
		return;
	}

	if ( cl->state != CS_ACTIVE ) {
		Com_Printf( "Client is not active.\n" );
		return;
	}

	if ( Cmd_Argc() >= 2 ) {
		s = Cmd_Argv( 1 );
		Q_strncpyz( demoName, s, sizeof( demoName ) );
		Com_sprintf( name, sizeof( name ), "demos/%s.dm_%d", demoName, PROTOCOL_VERSION );
	} else {
		// timestamp the file
		SV_DemoFilename( demoName, sizeof( demoName ) );

		Com_sprintf (name, sizeof(name), "demos/%s.dm_%d", demoName, PROTOCOL_VERSION );

		if ( FS_FileExists( name ) ) {
			Com_Printf( "Record: Couldn't create a file\n");
			return;
 		}
	}

	SV_RecordDemo( cl, demoName );
}

//===========================================================

/*
==================
SV_ClientMBClass
Helper Function to clients MBClass 
==================
*/
static int SV_ClientMBClass(client_t* cl) {

	// This function could do with working a LOT BETTER
	// Find pointer where class comes from MBII
	// At the moment, look through commands for one with MBC, ensure models match
	// After 1000 Commands pointers break, so this can stop working mid way through a round

	char* class_id = "0";
	char model = Info_ValueForKey(cl->userinfo, "model")[0];
	int i = 0;

	while (cl->reliableCommands && i < 1000) {
		if (cl->reliableCommands[i]) {
			if (strstr(cl->reliableCommands[i], "mbc") != NULL) {
				class_id = Info_ValueForKey(cl->reliableCommands[i], "mbc");
				if (*class_id) {
					char m_model = Info_ValueForKey(cl->reliableCommands[i], "m")[0];
					if (m_model == model) {
						break;
					}
				}
			}
		}
		i++;
	}

	return atoi(class_id);

}

/*
==================
SV_CompleteMapName
==================
*/
static void SV_CompleteMapName( char *args, int argNum ) {
	if ( argNum == 2 )
		Field_CompleteFilename( "maps", "bsp", qtrue, qfalse );
}

/*
==================
SV_ForceCvar_f_helper
Called internally by SV_ForceCvar_f
==================
*/
static void SV_ForceCvar_f_helper(client_t * cl) {
	char* val;
	int	len = -1;

	if (strlen(Cmd_Argv(3)) > 0) {
		val = Info_ValueForKey(cl->userinfo, Cmd_Argv(2));
		if (val[0])
			len = strlen(Cmd_Argv(3)) - strlen(val) + strlen(cl->userinfo);
		else
			len = strlen(Cmd_Argv(2)) + strlen(Cmd_Argv(3)) + 2 + strlen(cl->userinfo);
	}
	if (len >= MAX_INFO_STRING)
		SV_DropClient(cl, "userinfo string length exceeded");
	else {
		// In the case where Cmd_Argv(3) is "", the Info_SetValueForKey() call will
		// actually just call Info_RemoveKey().
		Info_SetValueForKey(cl->userinfo, Cmd_Argv(2), Cmd_Argv(3));
		SV_UserinfoChanged(cl);
		// call prog code to allow overrides
		VM_Call(currentVM, GAME_CLIENT_USERINFO_CHANGED, cl - svs.clients);

	}
}

/*
==================
SV_ForceCvar_f
Set a cvar for a user
==================
*/
static void SV_ForceCvar_f(void) {
	client_t* cl;

	// make sure server is running
	if (!com_sv_running->integer) {
		Com_Printf("Server is not running.\n");
		return;
	}

	if (Cmd_Argc() != 4 || strlen(Cmd_Argv(2)) == 0) {
		Com_Printf("Usage: forcecvar <player name> <cvar name> <cvar value>\nPlayer may be 'allbots'\n");
		return;
	}

	cl = SV_GetPlayerByHandle();
	if (!cl) {
		cl = SV_GetPlayerByNum();
		if (!cl) {
			return;
		}
	}

	SV_ForceCvar_f_helper(cl);
}


static void SV_Wannatest(void) {

	client_t* cl;

	// make sure server is running
	if (!com_sv_running->integer) {
		Com_Printf("Server is not running.\n");
		return;
	}

	cl = SV_GetPlayerByHandle();
	if (!cl) {
		cl = SV_GetPlayerByNum();
		if (!cl) {
			return;
		}
	}

	std::thread(SV_ClientTimedPowerup, cl, 5, MB_PW_INVINSIBLE).detach();

}

static void SV_GiveForce(void) {

	client_t* cl;

	cl = SV_GetPlayerByHandle();
	if (!cl) {
		cl = SV_GetPlayerByNum();
		if (!cl) {
			return;
		}
	}

	cl->gentity->playerState->fd.forcePowersKnown |= (1 << atoi(Cmd_Argv(2)));
	cl->gentity->playerState->fd.forcePower = 100;

}

/*
==================
SV_Wannacheat
Enable / Disable Cheats Without Dev Map
==================
*/

static void SV_WannaCheat(void) {

	// make sure server is running
	if (!com_sv_running->integer) {
		Com_Printf("Server is not running.\n");
		return;
	}

	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: wannacheat <1|0> \nEnable or Disable sv_cheats\n");
		return;
	}


	if (!strcmp(Cmd_Argv(1), "1")) {
		Com_Printf("Cheating Enabled\n");
	}
	else {
		Com_Printf("Cheating Disabled\n");
	}

	Cvar_Set("sv_cheats", Cmd_Argv(1));
	GVM_RunFrame(sv.time);
}

/*
==================
SV_Wannabe
Execute commands as a client
Runs with Cheats enabled
==================
*/
static void SV_Wannabe(void) {
	client_t* cl;

	// make sure server is running
	if (!com_sv_running->integer) {
		Com_Printf("Server is not running.\n");
		return;
	}

	if (Cmd_Argc() != 3) {
		Com_Printf("Usage: wannabe <client> <command> \nExecute command as a given client\n");
		return;
	}

	cl = SV_GetPlayerByHandle();
	if (!cl) {
		cl = SV_GetPlayerByNum();
		if (!cl) {
			return;
		}
	}

	Cvar_Set("sv_cheats", "1");
	GVM_RunFrame(sv.time);
	SV_ExecuteClientCommand(cl, Cmd_Argv(2), qtrue);
	Cvar_Set("sv_cheats", "0");
	GVM_RunFrame(sv.time);
}

/*
==================
Called when a user runs spin
==================
*/

void SV_Spin(client_t* cl) {

	int	  cooldown;
	char  tmp[50];
	char* playername;
	int mb_class;
	char* response;
	int   rando;
	int   spins;
	int min;
	int max;
	qboolean valid_spin;

	response = "";

	if (!com_sv_running->integer) {
		Com_Printf("Server is not running.\n");
		return;
	}

	playername = cl->name;

	SV_UserinfoChanged(cl);

	// Player is dead / spectating
	if (cl->gentity->playerState->persistant[PERS_TEAM] == TEAM_SPECTATOR) {
		SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_RED "%s" S_COLOR_WHITE "\"\n", "You must be alive to spin");
		return;
	}

	// Fetch the Class ID for the client
	mb_class = SV_ClientMBClass(cl);

	// Warn Dekas they cannot spin
	if (mb_class == MB_CLASS_DEKA) {
		SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_RED "%s" S_COLOR_WHITE "\"\n", "Dekas cant spin");
		return;
	}

	// Playing is in cooldown
	if (svs.time < cl->gentity->playerState->userInt1) {
		cooldown = (cl->gentity->playerState->userInt1 - svs.time) / 1000;

		response = "still in cooldown";
		if (cooldown > 1) {
			sprintf(tmp, "Spin CoolDown: %d seconds", cooldown);
		}
		else {
			sprintf(tmp, "Spin CoolDown: %d second", cooldown);
		}

		SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_MAGENTA "%s" S_COLOR_WHITE "\"\n", tmp);
		return;
		//memset
	}

	Cvar_Set("sv_cheats", "1");
	Cvar_SetValue("sv_cheats", 1);
	Cvar_SetCheatState();
	GVM_RunFrame(sv.time);

	valid_spin = qfalse;
	spins = 0;
	char* range = "";

	do {

		int weight[]{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		int weightTotal = 1;

		//Bowcaster exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_BOWCASTER)) && cl->gentity->playerState->ammo[MB_AMMO_BOWCASTER_DISRUPTOR] > 100) {
			weight[0] = 0;
		}
		else weight[0] = 5;

		//DC15 exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_DC15)) && cl->gentity->playerState->ammo[MB_AMMO_DC15_DLT20_ARM_BLASTER] > 100) {
			weight[1] = 0;
		}
		else weight[1] = 5;

		//Lightsaber exclusions: Already own a Lightsaber
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_LIGHTSABER))) {
			weight[2] = 0;
		}
		else weight[2] = 5;

		//Westar Pistol exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_WESTAR34)) && cl->gentity->playerState->ammo[MB_AMMO_WELSTAR34] > 100) {
			weight[3] = 0;
		}
		else weight[3] = 5;

		//Frag Grenade exclusions: Owned more than one Frag Grenade
		if (cl->gentity->playerState->ammo[MB_AMMO_FRAG_GRENADES] > 1) {
			weight[4] = 0;
		}
		else weight[4] = 5;

		//Pulse Grenade exclusions: Already owned AND own two
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_PULSE_GREN)) && cl->gentity->playerState->ammo[MB_AMMO_PULSE_GRENADES] > 1) {
			weight[5] = 0;
		}
		else weight[5] = 5;

		//Disruptor Rifle exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_DISRUPTOR)) && cl->gentity->playerState->ammo[MB_AMMO_BOWCASTER_DISRUPTOR] > 100) {
			weight[6] = 0;
		}
		else weight[6] = 5;

		//Projectile Rifle exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_PROJECTILE_RIFLE)) && cl->gentity->playerState->ammo[MB_AMMO_PROJECTILE_RIFLE] > 5) {
			weight[7] = 0;
		}
		else weight[7] = 5;

		//DC17 exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_DC17_PISTOL)) && cl->gentity->playerState->ammo[MB_AMMO_DC17_PISTOL] > 100) {
			weight[8] = 0;
		}
		else weight[8] = 5;

		//Rocket Launcher exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_ROCKET_LAUNCHER)) && cl->gentity->playerState->ammo[MB_AMMO_ROCKETS] > 2) {
			weight[9] = 0;
		}
		else weight[9] = 5;

		//T21 exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_T21)) && cl->gentity->playerState->ammo[MB_AMMO_T21_AMMO] > 100) {
			weight[10] = 0;
		}
		else weight[10] = 5;

		//50 Armor exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_ARMOR] > 200)) {
			weight[11] = 0;
		}
		else weight[11] = 5;

		//100 Armor exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_ARMOR] > 200)) {
			weight[12] = 0;
		}
		else weight[12] = 5;

		//Size change exclusion: Wookies, SBDs
		if (mb_class == MB_CLASS_WOOKIE || mb_class == MB_CLASS_SBD) {
			weight[13] = 0;
		}
		else weight[13] = 5;

		//Jetpack exclusion: Jetpack fuel over 50
		if ((cl->gentity->playerState->jetpackFuel > 50)) {
			weight[14] = 0;
		}
		else weight[14] = 5;

		//Cloak exclusion: Already owned
		if ((cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_CLOAK))) {
			weight[15] = 0;
		}
		else weight[15] = 5;

		//EWEB exclusion: Already owned
		if ((cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_EWEB))) {
			weight[16] = 0;
		}
		else weight[16] = 5;

		//Sentry Gun exclusion: Already owned
		if (cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SENTRY_GUN)) {
			weight[17] = 0;
		}
		else weight[17] = 5;

		//Seeker exclusion: Already owned
		if (cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SEEKER)) {
			weight[18] = 0;
		}
		else weight[18] = 5;

		//Bacta Tank exclusion: Already owned
		if (cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_MEDPAC_BIG)) {
			weight[19] = 0;
		}
		else weight[19] = 5;

		//Forcefield exclusion: Already owned
		if (cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SHIELD)) {
			weight[20] = 0;
		}
		else weight[20] = 5;

		//Tauntaun exclusion: none
		weight[21] = 5;
		//Swoopbike exclusion: none
		weight[22] = 5;
		//Sith Speeder exclusion: none
		weight[23] = 5;
		//Dewback exclusion: none
		weight[24] = 5;

		//DLT20 exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_DLT) && cl->gentity->playerState->ammo[MB_AMMO_DC15_DLT20_ARM_BLASTER] > 100)) {
			weight[25] = 0;
		}
		else weight[25] = 5;

		//Arm Blaster exclusions: Already owned AND good on ammo
		if ((cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_ARM_BLASTER) && cl->gentity->playerState->ammo[MB_AMMO_DC15_DLT20_ARM_BLASTER] > 100)) {
			weight[26] = 0;
		}
		else weight[26] = 5;

		//Random Force Power exclusions: Must have at least Jump 1
		if (cl->gentity->playerState->fd.forcePowerLevel[1] < 1) {
			weight[27] = 0;
		}
		else weight[27] = 5;

		//Force Sensitivity exclusions: No Jedi or Sith
		if ((mb_class == MB_CLASS_JEDI) || (mb_class == MB_CLASS_SITH)) {
			weight[28] = 0;
		}
		else weight[28] = 5;

		//Superpowers exclusions: none
		weight[29] = 5;

		//Accumulates total weighted chance of all outcomes (-1 because it needs to start at 0)
		for (auto& num : weight) {
			weightTotal += num - 1;
		}

		//Generates a random number between the weightTotal and 1 (excludes 0 because 0 is the exclusion list)
		srand(time(0));
		rando = rand() % weightTotal + 1;
		/*
		sprintf(range, "Range was: 1 - %d", weightTotal);
		SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_MAGENTA "%s" S_COLOR_WHITE "\"\n", range);
		sprintf(tmp, "Rolled Number Was: %d", rando);
		*/
		SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_MAGENTA "%s" S_COLOR_WHITE "\"\n", tmp);


		min = 1;
		max = weight[0];
		// gives a Bowcaster
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_BOWCASTER))) {
				cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_BOWCASTER);
				cl->gentity->playerState->ammo[MB_AMMO_BOWCASTER_DISRUPTOR] = 500;
				Com_Printf("Giving %s^7 a Bowcaster\n", playername);
				response = "You win a Bowcaster";
				valid_spin = qtrue;
				break;
			}
			else {
				cl->gentity->playerState->ammo[MB_AMMO_BOWCASTER_DISRUPTOR] += 200;
				Com_Printf("Giving %s^7 Bowcaster ammo\n", playername);
				response = "You win Bowcaster ammo!";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[0];
		max = max + weight[1];
		// gives a DC15
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_DC15))) {
				cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_DC15);
				cl->gentity->playerState->ammo[MB_AMMO_DC15_DLT20_ARM_BLASTER] = 500;
				Com_Printf("Giving %s^7 a DC15\n", playername);
				response = "You win a DC15";
				valid_spin = qtrue;
				break;
			}
			else {
				cl->gentity->playerState->ammo[MB_AMMO_DC15_DLT20_ARM_BLASTER] += 200;
				Com_Printf("Giving %s^7 DC15 ammo\n", playername);
				response = "You win DC15 ammo!";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[1];
		max = max + weight[2];
		// gives a Lightsaber and a random style
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			char* saberstyle_name = "";

			int saberstyles[] = { MB_SS_BLUE, MB_SS_YELLOW, MB_SS_RED };
			int rand_saberstyle = saberstyles[rand() % 2];
			cl->gentity->playerState->fd.saberAnimLevel = rand_saberstyle;
			cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_LIGHTSABER);
			cl->gentity->playerState->fd.saberAnimLevel = rand_saberstyle;

			if (rand_saberstyle == MB_SS_BLUE) saberstyle_name = "Blue";
			if (rand_saberstyle == MB_SS_YELLOW) saberstyle_name = "Yellow";
			if (rand_saberstyle == MB_SS_RED) saberstyle_name = "Red";

			cl->gentity->playerState->fd.forcePowerLevel[3] = 3;
			cl->gentity->playerState->fd.forcePowerLevel[14] = 3;
			cl->gentity->playerState->fd.forcePowerLevel[15] = 1;
			cl->gentity->playerState->fd.forcePowerLevel[16] = 3;

			Com_Printf("Giving %s^7 a Lightsaber with %s style\n", playername, saberstyle_name);
			Com_sprintf(tmp, sizeof(tmp), "You win a Lightsaber with %s style", saberstyle_name);
			response = tmp;
			valid_spin = qtrue;
			break;
		}

		min = min + weight[2];
		max = max + weight[3];
		// gives a Westar pistol
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_WESTAR34))) {
				cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_WESTAR34);
				cl->gentity->playerState->ammo[MB_AMMO_WELSTAR34] = 500;
				Com_Printf("Giving %s^7 a Westar 34\n", playername);
				response = "You win a Westar 34";
				valid_spin = qtrue;
				break;
			}
			else {
				cl->gentity->playerState->ammo[MB_AMMO_WELSTAR34] += 200;
				Com_Printf("Giving %s^7 Westar 34 ammo\n", playername);
				response = "You win Westar 34 ammo";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[3];
		max = max + weight[4];
		// gives 2 Frag Grenades
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_FRAG_GREN);
			cl->gentity->playerState->ammo[MB_AMMO_FRAG_GRENADES] += 2;
			Com_Printf("Giving %s^7 2 Frag Grenades\n", playername);
			response = "You win 2 Frag Grenades";
			valid_spin = qtrue;
			break;
		}

		min = min + weight[4];
		max = max + weight[5];
		// gives 2 Pulse Grenades
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_PULSE_GREN);
			cl->gentity->playerState->ammo[MB_AMMO_PULSE_GRENADES] += 2;
			Com_Printf("Giving %s^7 2 Pulse Grenades\n", playername);
			response = "You win 2 Pulse Grenades";
			valid_spin = qtrue;
			break;
		}

		min = min + weight[5];
		max = max + weight[6];
		// gives a Disruptor Rifle
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_DISRUPTOR))) {
				cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_DISRUPTOR);
				cl->gentity->playerState->ammo[MB_AMMO_BOWCASTER_DISRUPTOR] = 500;
				Com_Printf("Giving %s^7 a Disruptor Rifle\n", playername);
				response = "You win a Disruptor Rifle";
				valid_spin = qtrue;
				break;
			}
			else {
				cl->gentity->playerState->ammo[MB_AMMO_BOWCASTER_DISRUPTOR] += 200;
				Com_Printf("Giving %s^7 Disruptor Rifle ammo\n", playername);
				response = "You win Disruptor Rifle ammo";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[6];
		max = max + weight[7];
		// gives a Projectile Rifle
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_PROJECTILE_RIFLE))) {
				cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_PROJECTILE_RIFLE);
				cl->gentity->playerState->ammo[MB_AMMO_PROJECTILE_RIFLE] = 10;
				Com_Printf("Giving %s^7 a Projectile Rifle\n", playername);
				response = "You win a Projectile Rifle";
				valid_spin = qtrue;
				break;
			}
			else {
				cl->gentity->playerState->ammo[MB_AMMO_PROJECTILE_RIFLE] += 5;
				Com_Printf("Giving %s^7 Projectile Rifle ammo\n", playername);
				response = "You win Projectile Rifle ammo";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[7];
		max = max + weight[8];
		// gives a DC17 pistol
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_DC17_PISTOL))) {
				cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_DC17_PISTOL);
				cl->gentity->playerState->ammo[MB_AMMO_DC17_PISTOL] = 500;
				Com_Printf("Giving %s^7 an DC17 Pistol\n", playername);
				response = "You win a DC17 Pistol";
				valid_spin = qtrue;
				break;
			}
			else {
				cl->gentity->playerState->ammo[MB_AMMO_DC17_PISTOL] += 200;
				Com_Printf("Giving %s^7 DC17 ammo\n", playername);
				response = "You win DC17 ammo";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[8];
		max = max + weight[9];
		// gives a Rocket Launcher
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (mb_class != MB_CLASS_ARC) { // Exclude Arcs
				if (!(cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_ROCKET_LAUNCHER))) {
					cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_ROCKET_LAUNCHER);
					cl->gentity->playerState->ammo[MB_AMMO_ROCKETS] = 3;
					Com_Printf("Giving %s^7 a Rocket Launcher\n", playername);
					response = "You win a Rocket Launcher";
					valid_spin = qtrue;
					break;
				}
				else {
					cl->gentity->playerState->ammo[MB_AMMO_ROCKETS] += 2;
					Com_Printf("Giving %s^7 Rockets\n", playername);
					response = "You win Rockets";
					valid_spin = qtrue;
					break;
				}
			}
		}

		min = min + weight[9];
		max = max + weight[10];
		// gives a T21
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_T21))) {
				cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_T21);
				cl->gentity->playerState->ammo[MB_AMMO_T21_AMMO] = 500;
				Com_Printf("Giving %s^7 a T21\n", playername);
				response = "You win a T21";
				valid_spin = qtrue;
				break;
			}
			else {
				cl->gentity->playerState->ammo[MB_AMMO_T21_AMMO] += 200;
				Com_Printf("Giving %s^7 T21 ammo\n", playername);
				response = "You win T21 ammo";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[10];
		max = max + weight[11];
		// gives 50 Armor
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			cl->gentity->playerState->stats[STAT_ARMOR] += 50;
			Com_Printf("Giving %s^7 50 Armor\n", playername);
			response = "You win 50 Armor";
			valid_spin = qtrue;
			break;
		}

		min = min + weight[11];
		max = max + weight[12];
		// gives 100 Armor
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			cl->gentity->playerState->stats[STAT_ARMOR] += 100;
			Com_Printf("Giving %s^7 100 Armor\n", playername);
			response = "You win 100 Armor";
			valid_spin = qtrue;
			break;
		}

		min = min + weight[12];
		max = max + weight[13];
		// changes player size
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			int rand_size = rand() % 5;

			if (rand_size == 0 && cl->gentity->playerState->iModelScale != 60) {
				cl->gentity->playerState->iModelScale = 50;
				cl->gentity->playerState->stats[STAT_MAX_HEALTH] -= 20;
				cl->gentity->playerState->stats[STAT_HEALTH] = cl->gentity->playerState->stats[STAT_MAX_HEALTH];
				Com_Printf("Making %s^7 very small\n", playername);
				response = "You've gotten much smaller!";
			}
			if (rand_size == 1 && cl->gentity->playerState->iModelScale != 80) {
				cl->gentity->playerState->iModelScale = 75;
				cl->gentity->playerState->stats[STAT_MAX_HEALTH] -= 10;
				cl->gentity->playerState->stats[STAT_HEALTH] = cl->gentity->playerState->stats[STAT_MAX_HEALTH];
				Com_Printf("Making %s^7 small\n", playername);
				response = "You've gotten a bit smaller";
			}
			if (rand_size == 2 && cl->gentity->playerState->iModelScale != 100) {
				cl->gentity->playerState->iModelScale = 100;
				cl->gentity->playerState->stats[STAT_MAX_HEALTH] = 100;
				cl->gentity->playerState->stats[STAT_HEALTH] = cl->gentity->playerState->stats[STAT_MAX_HEALTH];
				Com_Printf("Making %s^7 normal-sized\n", playername);
				response = "You're normal sized again";
			}
			if (rand_size == 3 && cl->gentity->playerState->iModelScale != 120) {
				cl->gentity->playerState->iModelScale = 120;
				cl->gentity->playerState->stats[STAT_MAX_HEALTH] += 20;
				cl->gentity->playerState->stats[STAT_HEALTH] = cl->gentity->playerState->stats[STAT_MAX_HEALTH];
				Com_Printf("Making %s^7 bigger\n", playername);
				response = "You've gotten somewhat bigger";
			}
			if (rand_size == 4 && cl->gentity->playerState->iModelScale != 140) {
				cl->gentity->playerState->iModelScale = 150;
				cl->gentity->playerState->stats[STAT_MAX_HEALTH] += 50;
				cl->gentity->playerState->stats[STAT_HEALTH] = cl->gentity->playerState->stats[STAT_MAX_HEALTH];
				Com_Printf("Making %s^7 very big\n", playername);
				response = "You're gargantuan!";
			}
			valid_spin = qtrue;
			break;
		}

		min = min + weight[13];
		max = max + weight[14];
		// win Jetpack
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (mb_class != MB_CLASS_MANDO && mb_class != MB_CLASS_SBD) { // Exclude Mandos
				if (!(cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_JETPACK))) {
					cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_JETPACK);
					cl->gentity->playerState->jetpackFuel = 100;
					Com_Printf("Giving %s^7 a Jetpack\n", playername);
					response = "You win a Jetpack ";
					valid_spin = qtrue;
					break;
				}
				else {
					cl->gentity->playerState->jetpackFuel += 50;
					Com_Printf("Giving %s^7 Jetpack fuel\n", playername);
					response = "You win Jetpack fuel";
					valid_spin = qtrue;
					break;
				}
			}
		}

		min = min + weight[14];
		max = max + weight[15];
		// win Cloak Generator
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_CLOAK))) {
				cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_CLOAK);
				cl->gentity->playerState->cloakFuel = 100;
				Com_Printf("Giving %s^7 a Cloak Generator\n", playername);
				response = "You win a Cloak Generator ";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[15];
		max = max + weight[16];
		// win EWEB
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_EWEB))) {
				cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_EWEB);
				Com_Printf("Giving %s^7 an EWEB Gun Emplacement\n", playername);
				response = "You win an EWEB Gun Emplacement";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[16];
		max = max + weight[17];
		// win Sentry Gun
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SENTRY_GUN))) {
				cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_SENTRY_GUN);
				Com_Printf("Giving %s^7 a Automated Sentry Gun\n", playername);
				response = "You win an Automated Sentry Gun";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[17];
		max = max + weight[18];
		// win Seeker Droid
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SEEKER))) {
				cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_SEEKER);
				Com_Printf("Giving %s ^7 a Seeker Droid\n", playername);
				response = "You win a Seeker Droid";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[18];
		max = max + weight[19];
		// win Bacta Tank
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_MEDPAC_BIG))) {
				cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_MEDPAC_BIG);
				Com_Printf("Giving %s ^7 a Tank of Bacta\n", playername);
				response = "You win a Tank of Bacta";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[19];
		max = max + weight[20];
		// win Forcefield
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SHIELD))) {
				cl->gentity->playerState->stats[STAT_HOLDABLE_ITEMS] |= (1 << HI_SHIELD);
				Com_Printf("Giving %s ^7 a Forcefield Generator\n", playername);
				response = "You win a Forcefield Generator";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[20];
		max = max + weight[21];
		// win Tauntaun
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			SV_ExecuteClientCommandDelayed(cl, "npc spawn vehicle tauntaun", 5);
			Com_Printf("Giving %s ^7 a TaunTaun\n", playername);
			response = "You win a TaunTaun " SPAWN_VEHICLE_SUFFIX;
			valid_spin = qtrue;
			break;
		}

		min = min + weight[21];
		max = max + weight[22];
		// win Speeder Bike
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			Com_Printf("Giving %s ^7 a Swoop Bike\n", playername);
			int rand_swoop = rand() % 6;

			if (rand_swoop == 0) {
				response = "You win a Blue Swoop Bike " SPAWN_VEHICLE_SUFFIX;
				SV_ExecuteClientCommandDelayed(cl, "npc spawn vehicle swoop_mp2", 5);
			}

			if (rand_swoop == 1) {
				response = "You win a Red Swoop Bike " SPAWN_VEHICLE_SUFFIX;
				SV_ExecuteClientCommandDelayed(cl, "npc spawn vehicle swoop_mp", 5);
			}

			if (rand_swoop == 2) {
				response = "You win a Battle Swoop Bike " SPAWN_VEHICLE_SUFFIX;
				SV_ExecuteClientCommandDelayed(cl, "npc spawn vehicle swoop_battle_cunning", 5);
			}

			if (rand_swoop == 3) {
				response = "You win a Racing Swoop Bike " SPAWN_VEHICLE_SUFFIX;
				SV_ExecuteClientCommandDelayed(cl, "npc spawn vehicle swoop_race_b", 5);
			}

			if (rand_swoop == 4) {
				response = "You win a Yavin Swoop Bike " SPAWN_VEHICLE_SUFFIX;
				SV_ExecuteClientCommandDelayed(cl, "npc spawn vehicle yavin_swoop", 5);
			}

			if (rand_swoop == 5) {
				response = "You win a Swoop Bike " SPAWN_VEHICLE_SUFFIX;
				SV_ExecuteClientCommandDelayed(cl, "npc spawn vehicle swoop_mp2", 5);
			}

			valid_spin = qtrue;
			break;
		}

		min = min + weight[22];
		max = max + weight[23];
		// win Sith Speeder
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			Com_Printf("Giving %s ^7 a Sith Speeder\n", playername);
			response = "You win a Sith Speeder " SPAWN_VEHICLE_SUFFIX;
			SV_ExecuteClientCommandDelayed(cl, "npc spawn vehicle sithspeeder_mst", 5);
			valid_spin = qtrue;
			break;
		}

		min = min + weight[23];
		max = max + weight[24];
		// win Dewback
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			Com_Printf("Giving %s ^7 a Dewback\n", playername);
			response = "You win a Dewback " SPAWN_VEHICLE_SUFFIX;
			SV_ExecuteClientCommandDelayed(cl, "npc spawn vehicle dewback", 5);
			valid_spin = qtrue;
			break;
		}

		min = min + weight[24];
		max = max + weight[25];
		// win DLT20
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_DLT))) {
				cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_DLT);
				cl->gentity->playerState->ammo[MB_AMMO_DC15_DLT20_ARM_BLASTER] = 500;
				Com_Printf("Giving %s ^7an a DLT20\n", playername);
				response = "You win a DLT20";
				valid_spin = qtrue;
				break;
			}
			else {
				cl->gentity->playerState->ammo[MB_AMMO_DC15_DLT20_ARM_BLASTER] += 200;
				Com_Printf("Giving %s ^7 DLT20 ammo\n", playername);
				response = "You win DLT20 ammo";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[25];
		max = max + weight[26];
		// win Arm Blaster
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {
			if (!(cl->gentity->playerState->stats[STAT_WEAPONS] & (1 << MB_ARM_BLASTER))) {
				cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_ARM_BLASTER);
				cl->gentity->playerState->ammo[MB_AMMO_DC15_DLT20_ARM_BLASTER] = 500;
				Com_Printf("Giving %s ^7an a Arm Blaster\n", playername);
				response = "You win an Arm Blaster";
				valid_spin = qtrue;
				break;
			}
			else {
				cl->gentity->playerState->ammo[MB_AMMO_DC15_DLT20_ARM_BLASTER] += 200;
				Com_Printf("Giving %s ^7 Arm Blaster ammo\n", playername);
				response = "You win Arm Blaster ammo";
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[26];
		max = max + weight[27];
		// win Random Force Power
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {

			int forcepowers[] = { MB_FORCE_SPEED, MB_FORCE_LIGHTNING, MB_FORCE_PUSH, MB_FORCE_PULL, MB_FORCE_MIND_TRICK, MB_FORCE_SENSE };
			int rand_forcepower = forcepowers[rand() % 6];
			char tmp[200];

			if (!(cl->gentity->playerState->fd.forcePowersKnown & (1 << rand_forcepower))) {
				cl->gentity->playerState->fd.forcePowersKnown |= (1 << rand_forcepower);
				cl->gentity->playerState->fd.forcePowerLevel[rand_forcepower] = 3;

				char* forcepower_name = "";

				if (rand_forcepower == MB_FORCE_SPEED) forcepower_name = "Force Speed";
				if (rand_forcepower == MB_FORCE_LIGHTNING) forcepower_name = "Force Lightning";
				if (rand_forcepower == MB_FORCE_PUSH) forcepower_name = "Force Push";
				if (rand_forcepower == MB_FORCE_PULL) forcepower_name = "Force Pull";
				if (rand_forcepower == MB_FORCE_MIND_TRICK) forcepower_name = "Mind Trick";
				if (rand_forcepower == MB_FORCE_SENSE) forcepower_name = "Force Sense";

				Com_Printf("Giving %s ^7%s\n", playername, forcepower_name);
				Com_sprintf(tmp, sizeof(tmp), "You win %s", forcepower_name);
				response = tmp;
				valid_spin = qtrue;
				break;
			}
		}

		min = min + weight[27];
		max = max + weight[28];
		// win Force Sensitivity
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {

			cl->gentity->playerState->fd.forcePower = 300;
			cl->gentity->playerState->fd.forcePowerMax = 300;
			cl->gentity->playerState->fd.forcePowerLevel[1] = 2;
			cl->gentity->playerState->fd.forcePowerLevel[3] = 3;
			cl->gentity->playerState->fd.forcePowerLevel[14] = 3;
			cl->gentity->playerState->fd.forcePowerLevel[15] = 1;
			cl->gentity->playerState->fd.forcePowerLevel[16] = 3;

			cl->gentity->playerState->fd.forcePowersKnown |= (1 << MB_FORCE_PULL);
			cl->gentity->playerState->fd.forcePowerLevel[MB_FORCE_SPEED] = FORCE_LEVEL_2;
			cl->gentity->playerState->fd.forcePowersKnown |= (1 << MB_FORCE_PUSH);
			cl->gentity->playerState->fd.forcePowerLevel[MB_FORCE_SPEED] = FORCE_LEVEL_2;
			cl->gentity->playerState->fd.forcePowersKnown |= (1 << MB_FORCE_SPEED);
			cl->gentity->playerState->fd.forcePowerLevel[MB_FORCE_SPEED] = FORCE_LEVEL_2;
			cl->gentity->playerState->fd.forcePowersKnown |= (1 << MB_FORCE_SENSE);
			cl->gentity->playerState->fd.forcePowerLevel[MB_FORCE_SENSE] = FORCE_LEVEL_2;

			Com_Printf("Giving %s ^7 Force Sensitivity\n", playername);
			response = "You win Force Sensitivity";
			valid_spin = qtrue;
			break;
		}

		min = min + weight[28];
		max = max + weight[29];
		// won a random superspin
		if ((unsigned)(rando) >= (min) && (rando) <= (max)) {

			char* superpower = "";
			int rand_superpower = rand() % 1;

			if (rand_superpower == 0) {
				// win Shinrar Mech
				Com_Printf("Giving %s ^7 a Shinrar Mech\n", playername);
				SV_ExecuteClientCommandDelayed(cl, "npc spawn vehicle shinraR", 5);
				SV_SendServerCommand(NULL, "chat \"" SVSAY_PREFIX "%s won a Shinrar Mech! We're in the end-game now...\"\n", playername);
				superpower = "Shinrar Mech " SPAWN_VEHICLE_SUFFIX;
			}
			else if (rand_superpower == 1) {
				// win Invincibility
				Com_Printf("Giving %s ^7 Invincibility\n", playername);
				SV_ExecuteClientCommand(cl, "god", qtrue);
				GVM_RunFrame(sv.time);
				SV_ExecuteClientCommandDelayed(cl, "god", 30);
				SV_ClientTimedPowerup(cl, MB_PW_INVINSIBLE, 30);
				superpower = "Temporary Invincibility";
			}

			response = "You've won a Superspin: %s", superpower;
			valid_spin = qtrue;
			break;
		}

		spins++;

	} while (valid_spin == qfalse || spins < 20);

	if (spins == 20) {
		response = "Something went wrong with you spin. We did 20 spins and you won nothing all 20 times...report to admin";
	}

	Cvar_Set("sv_cheats", "0");
	// Next Spin Time
	cl->gentity->playerState->userInt1 = svs.time + 4000;
	//Tells the player the set response
	SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_MAGENTA "%s" S_COLOR_WHITE "\"\n", response);
	return;
}

void SV_Buy(client_t* cl) {

	char  tmp[50];
	char* playername;
	int mb_class;
	char* response;
	const char* group[5] { "Pistols", "Rifles", "Heavy", "Equipment", "Abilities" };
	const char* pistols[4] { "Bryar", "DL44", "Westar", "ARC" };
	const char* rifles[6] { "E-11", "Bowcaster", "DC15-S", "DC15-A", "A280", "T-21" };
	const char* support[8] { "Rocket", "Projectile", "Disruptor", "Chaingun", "Frags", "Stuns", "Fire", "Thermal" };
	const char* equipment[8] { "Jetpack", "Bacta", "Forcefield", "Autoturret", "Emplacement", "Stealth", "Armor", "Health" };
	const char* abilities[8] { "Force-Kit", "Bounty-Kit", "Hero-Kit", "ARC-Kit", "Mando-Kit", "Wookie-Kit", "Reinforce", "Squad" };

	std::string fullCommand = "figure our how to forward the command that triggers the !buy function";
	std::size_t space = fullCommand.find(" ");
	std::string purchase = fullCommand.substr(space + 1); //separates the secondary command from the !buy command

	response = "";

	if (!com_sv_running->integer) {
		Com_Printf("Server is not running.\n");
		return;
	}

	playername = cl->name;

	SV_UserinfoChanged(cl);
	// Disables if Player is dead or spectating
	if (cl->gentity->playerState->persistant[PERS_TEAM] == TEAM_SPECTATOR) {
		SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_RED "%s" S_COLOR_WHITE "\"\n", "You must be alive to buy things");
		return;
	}

	// Fetch the Class ID for the client
	mb_class = SV_ClientMBClass(cl);

	// Warn Dekas they cannot spin
	if (mb_class == MB_CLASS_DEKA) {
		SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_RED "%s" S_COLOR_WHITE "\"\n", "Dekas cannot buy anything.");
		return;
	}

	Cvar_Set("sv_cheats", "1");
	Cvar_SetValue("sv_cheats", 1);
	Cvar_SetCheatState();
	GVM_RunFrame(sv.time);

	cl->gentity->playerState->fd.saberAnimLevel = MB_SS_YELLOW;
	cl->gentity->playerState->stats[STAT_WEAPONS] |= (1 << MB_LIGHTSABER);
	cl->gentity->playerState->fd.saberAnimLevel = MB_SS_YELLOW;
	cl->gentity->playerState->fd.forcePowerLevel[3] = 3;
	cl->gentity->playerState->fd.forcePowerLevel[14] = 3;
	cl->gentity->playerState->fd.forcePowerLevel[15] = 1;
	cl->gentity->playerState->fd.forcePowerLevel[16] = 3;

	/*
	weapon_mbii_t
	weapon_t
	powerups_mbii_t
	powerup_t

	SABERTYPE
	arc
	broad
	claw
	dagger
	lance
	prong
	sai
	single
	sith_sword
	staff
	star

	SLF2 - real weapon modifications

	SV_ClientMBClass
	SV_lanForceRate
	WeaponData

	WP_Blaster
	WP_BryarPistol
	WP_Emplaced_Gun
	WP_Turret
	*/

	Cvar_Set("sv_cheats", "0");
	response = "Congrats on your purchase!!!";
	SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_MAGENTA "%s" S_COLOR_WHITE "\"\n", response);
	return;
}

void SV_Help(client_t* cl) {
	SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_RED "%s" S_COLOR_WHITE "\"\n", "Use ^7!buy ^2to purchase weapons, equipment, and abilities");
	SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_RED "%s" S_COLOR_WHITE "\"\n", "Use ^7!funds ^2to see how much money you currently have");
	SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_RED "%s" S_COLOR_WHITE "\"\n", "Use ^7!spin ^2to get random stuff");
	return;
}

void SV_Funds(client_t* cl) {
	SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_RED "%s" S_COLOR_WHITE "\"\n", "You're a broke bitch");
	return;
}

void SV_Upgrade(client_t* cl) {

	int	  cooldown;
	char  tmp[50];
	char* playername;
	int mb_class;
	char* response;

	response = "";

	if (!com_sv_running->integer) {
		Com_Printf("Server is not running.\n");
		return;
	}

	playername = cl->name;

	SV_UserinfoChanged(cl);

	// Player is dead / spectating
	if (cl->gentity->playerState->persistant[PERS_TEAM] == TEAM_SPECTATOR) {
		SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_RED "%s" S_COLOR_WHITE "\"\n", "You must be alive to spin");
		return;
	}

	// Fetch the Class ID for the client
	mb_class = SV_ClientMBClass(cl);

	// Warn Dekas they cannot spin
	if (mb_class == MB_CLASS_DEKA) {
		SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_RED "%s" S_COLOR_WHITE "\"\n", "Dekas cant spin");
		return;
	}

	// Playing is in cooldown
	if (svs.time < cl->gentity->playerState->userInt1) {
		cooldown = (cl->gentity->playerState->userInt1 - svs.time) / 1000;

		response = "still in cooldown";
		if (cooldown > 1) {
			sprintf(tmp, "Spin CoolDown: %d seconds", cooldown);
		}
		else {
			sprintf(tmp, "Spin CoolDown: %d second", cooldown);
		}

		SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_MAGENTA "%s" S_COLOR_WHITE "\"\n", tmp);
		return;
		//memset
	}

	Cvar_Set("sv_cheats", "1");
	Cvar_SetValue("sv_cheats", 1);
	Cvar_SetCheatState();
	GVM_RunFrame(sv.time);

	if (mb_class == MB_CLASS_ARC) {

	}

	if (mb_class == MB_CLASS_BOUNTY_HUNTER) {

	}

	if (mb_class == MB_CLASS_CLONE) {
		//Equipment or Weapons
	}

	if (mb_class == MB_CLASS_COMMANDER) {
		//Equipment or Weapons
	}

	if (mb_class == MB_CLASS_DEKA) {
		//Nothing
	}

	if (mb_class == MB_CLASS_ELITE_SOLDER) {
		//Equipment items
	}

	if (mb_class == MB_CLASS_HERO) {
		//Equipment Items
	}

	if (mb_class == MB_CLASS_JEDI) {
		//Depends on Saber Style
		//Blue: 
		//Red: Armor
		//Yellow: Pistol
		//Cyan: Dash
		//Purple: Wookie Stagger
		//Staff or Duals: Arc Roll
	}

	if (mb_class == MB_CLASS_MANDO) {
		//Beskar
	}

	if (mb_class == MB_CLASS_SBD) {
		//Rocket Launcher
	}

	if (mb_class == MB_CLASS_SITH) {
		//Armor
	}

	if (mb_class == MB_CLASS_SOLDER) {
		//Clone Chaingun
	}

	if (mb_class == MB_CLASS_WOOKIE) {
		//Nothing, fuck Wookies
	}

	Cvar_Set("sv_cheats", "0");
	// Next Spin Time
	cl->gentity->playerState->userInt1 = svs.time + 4000;
	//Tells the player the set response
	SV_SendServerCommand(cl, "chat \"" SVTELL_PREFIX S_COLOR_MAGENTA "%s" S_COLOR_WHITE "\"\n", response);
	return;
}
/*
==================
SV_AddOperatorCommands
==================
*/
void SV_AddOperatorCommands( void ) {
	static qboolean	initialized;

	if ( initialized ) {
		return;
	}
	initialized = qtrue;
	Cmd_AddCommand("wannatest", SV_Wannatest, "");
	Cmd_AddCommand("GiveForce", SV_GiveForce, "");
		
	Cmd_AddCommand ("heartbeat", SV_Heartbeat_f, "Sends a heartbeat to the masterserver" );
	Cmd_AddCommand ("kick", SV_Kick_f, "Kick a user from the server" );
	Cmd_AddCommand ("kickbots", SV_KickBots_f, "Kick all bots from the server" );
	Cmd_AddCommand ("kickall", SV_KickAll_f, "Kick all users from the server" );
	Cmd_AddCommand ("kicknum", SV_KickNum_f, "Kick a user from the server by userid" );
	Cmd_AddCommand ("clientkick", SV_KickNum_f, "Kick a user from the server by userid" );
	Cmd_AddCommand ("status", SV_Status_f, "Prints status of server and connected clients" );
	Cmd_AddCommand ("serverinfo", SV_Serverinfo_f, "Prints the serverinfo that is visible in the server browsers" );
	Cmd_AddCommand ("systeminfo", SV_Systeminfo_f, "Prints the systeminfo variables that are replicated to clients" );
	Cmd_AddCommand ("dumpuser", SV_DumpUser_f, "Prints the userinfo for a given userid" );
	Cmd_AddCommand ("map_restart", SV_MapRestart_f, "Restart the current map" );
	Cmd_AddCommand ("sectorlist", SV_SectorList_f);
	Cmd_AddCommand ("map", SV_Map_f, "Load a new map with cheats disabled" );
	Cmd_SetCommandCompletionFunc( "map", SV_CompleteMapName );
	Cmd_AddCommand ("devmap", SV_Map_f, "Load a new map with cheats enabled" );
	Cmd_SetCommandCompletionFunc( "devmap", SV_CompleteMapName );
//	Cmd_AddCommand ("devmapbsp", SV_Map_f);	// not used in MP codebase, no server BSP_cacheing
	Cmd_AddCommand ("devmapmdl", SV_Map_f, "Load a new map with cheats enabled" );
	Cmd_SetCommandCompletionFunc( "devmapmdl", SV_CompleteMapName );
	Cmd_AddCommand ("devmapall", SV_Map_f, "Load a new map with cheats enabled" );
	Cmd_SetCommandCompletionFunc( "devmapall", SV_CompleteMapName );
	Cmd_AddCommand ("killserver", SV_KillServer_f, "Shuts the server down and disconnects all clients" );
	Cmd_AddCommand ("svsay", SV_ConSay_f, "Broadcast server messages to clients" );
	Cmd_AddCommand ("svtell", SV_ConTell_f, "Private message from the server to a user" );
	Cmd_AddCommand ("forcetoggle", SV_ForceToggle_f, "Toggle g_forcePowerDisable bits" );
	Cmd_AddCommand ("weapontoggle", SV_WeaponToggle_f, "Toggle g_weaponDisable bits" );
	Cmd_AddCommand ("svrecord", SV_Record_f, "Record a server-side demo" );
	Cmd_AddCommand ("svstoprecord", SV_StopRecord_f, "Stop recording a server-side demo" );
	Cmd_AddCommand ("sv_rehashbans", SV_RehashBans_f, "Reloads banlist from file" );
	Cmd_AddCommand ("sv_listbans", SV_ListBans_f, "Lists bans" );
	Cmd_AddCommand ("sv_banaddr", SV_BanAddr_f, "Bans a user" );
	Cmd_AddCommand ("sv_exceptaddr", SV_ExceptAddr_f, "Adds a ban exception for a user" );
	Cmd_AddCommand ("sv_bandel", SV_BanDel_f, "Removes a ban" );
	Cmd_AddCommand ("sv_exceptdel", SV_ExceptDel_f, "Removes a ban exception" );
	Cmd_AddCommand ("sv_flushbans", SV_FlushBans_f, "Removes all bans and exceptions" );
	Cmd_AddCommand ("forcecvar", SV_ForceCvar_f);
	Cmd_AddCommand ("wannacheat", SV_WannaCheat);
	Cmd_AddCommand ("wannabe", SV_Wannabe);
}

/*
==================
SV_RemoveOperatorCommands
==================
*/
void SV_RemoveOperatorCommands( void ) {
#if 0
	// removing these won't let the server start again
	Cmd_RemoveCommand ("heartbeat");
	Cmd_RemoveCommand ("kick");
	Cmd_RemoveCommand ("banUser");
	Cmd_RemoveCommand ("banClient");
	Cmd_RemoveCommand ("status");
	Cmd_RemoveCommand ("serverinfo");
	Cmd_RemoveCommand ("systeminfo");
	Cmd_RemoveCommand ("dumpuser");
	Cmd_RemoveCommand ("map_restart");
	Cmd_RemoveCommand ("sectorlist");
	Cmd_RemoveCommand ("svsay");
#endif
}
