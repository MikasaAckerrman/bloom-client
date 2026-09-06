/* test_menu_probe.c -- the missing "MenuFactory" must be reported ONCE.
 *
 * REPORTED SYMPTOM: an error box about MenuFactory on start plus console spam,
 * while another launcher (Freaky) shows nothing. Both are correct observations:
 * the native menu is OPTIONAL -- when the host app exports no MenuFactory the
 * client falls back to the cfg touch menus, which is a complete path. A launcher
 * that ships the object never sees the message; one that does not saw it on EVERY
 * connect, because HUD_Init runs per connect and g_pMenu stays null.
 *
 * This mirrors LoadMenuInterface()'s control flow from cl_dll/cdll_int.cpp
 * (which cannot be compiled on the host -- it needs the engine and mainui).
 * scripts/kf_menu_check.py proves the mirror still matches the source.
 */
#include <stdio.h>
#include <string.h>

/* ---- fake engine/host surface ------------------------------------------ */

static int   g_mobileAPI;        /* g_iMobileAPIVersion */
static int   g_haveGetNative;    /* gMobileAPI.pfnGetNativeObject != NULL */
static int   g_hostHasFactory;   /* host exports "MenuFactory" */
static int   g_factoryAccepts;   /* factory returns the requested interface */

static int   n_conPrint;         /* visible console lines */
static int   n_conDPrint;        /* developer-only lines */
static int   n_sysWarn;          /* modal warning boxes */
static char  last_line[256];

static void Con_Printf( const char *s )  { n_conPrint++;  snprintf( last_line, sizeof( last_line ), "%s", s ); }
static void Con_DPrintf( const char *s ) { n_conDPrint++; snprintf( last_line, sizeof( last_line ), "%s", s ); }

/* ---- mirror of LoadMenuInterface() ------------------------------------- */

static void *g_pMenu;
static int   s_menuProbed;

static void *GetNativeMenuExports( void )
{
	if( !g_mobileAPI || !g_haveGetNative )
		return NULL;
	if( !g_hostHasFactory )
		return NULL;
	return g_factoryAccepts ? (void *)1 : NULL;
}

static void LoadMenuInterface( void )
{
	if( g_pMenu )
		return;

	g_pMenu = GetNativeMenuExports();
	if( g_pMenu || s_menuProbed )
		return;

	s_menuProbed = 1;

	if( !g_mobileAPI || !g_haveGetNative )
		Con_DPrintf( "Menu: no mobile API, using touch menus\n" );
	else if( !g_hostHasFactory )
		Con_DPrintf( "Menu: host exports no \"MenuFactory\", using touch menus\n" );
	else
		Con_Printf( "Menu: \"MenuFactory\" does not provide IGameMenuExports, using touch menus\n" );
}

/* ------------------------------------------------------------------------ */

static int fails;

static void check( int cond, const char *what )
{
	if( cond )
		printf( "  ok    %s\n", what );
	else
	{
		printf( "  FAIL  %s\n", what );
		fails++;
	}
}

static void reset( void )
{
	g_pMenu = NULL;
	s_menuProbed = 0;
	n_conPrint = n_conDPrint = n_sysWarn = 0;
	last_line[0] = '\0';
}

/* 30 connects: a long session with many map changes. */
#define CONNECTS 30

int main( void )
{
	int i;

	printf( "=== launcher without MenuFactory (the reported case) ===\n" );
	reset();
	g_mobileAPI = 1; g_haveGetNative = 1; g_hostHasFactory = 0; g_factoryAccepts = 0;
	for( i = 0; i < CONNECTS; i++ )
		LoadMenuInterface();
	check( n_conDPrint == 1, "one developer line after 30 connects (was 30)" );
	check( n_conPrint == 0, "nothing on the normal console" );
	check( n_sysWarn == 0, "NO modal warning box" );
	check( strstr( last_line, "touch menus" ) != NULL, "message says what happens instead" );
	printf( "        -> %s", last_line );

	printf( "\n=== no mobile API at all (desktop) ===\n" );
	reset();
	g_mobileAPI = 0; g_haveGetNative = 0;
	for( i = 0; i < CONNECTS; i++ )
		LoadMenuInterface();
	check( n_conDPrint == 1 && n_conPrint == 0, "one developer line, silent console" );

	printf( "\n=== factory present but refuses the interface (version mismatch) ===\n" );
	/* This one IS visible: it means a real mismatch, not a launcher that simply
	 * has no native menu. Still exactly once. */
	reset();
	g_mobileAPI = 1; g_haveGetNative = 1; g_hostHasFactory = 1; g_factoryAccepts = 0;
	for( i = 0; i < CONNECTS; i++ )
		LoadMenuInterface();
	check( n_conPrint == 1, "visible line exactly once" );
	check( n_conDPrint == 0, "not demoted to a developer line" );
	check( strstr( last_line, "does not provide" ) != NULL, "names the cause" );
	printf( "        -> %s", last_line );

	printf( "\n=== launcher WITH MenuFactory (Freaky-like) ===\n" );
	reset();
	g_mobileAPI = 1; g_haveGetNative = 1; g_hostHasFactory = 1; g_factoryAccepts = 1;
	for( i = 0; i < CONNECTS; i++ )
		LoadMenuInterface();
	check( g_pMenu != NULL, "menu interface acquired" );
	check( n_conPrint == 0 && n_conDPrint == 0, "completely silent" );

	printf( "\n=== host starts exporting the object mid-session ===\n" );
	/* The probe latch must not prevent a LATER success: only the MESSAGE is
	 * once-only, the lookup itself still runs while g_pMenu is null. */
	reset();
	g_mobileAPI = 1; g_haveGetNative = 1; g_hostHasFactory = 0; g_factoryAccepts = 0;
	LoadMenuInterface();
	check( g_pMenu == NULL && n_conDPrint == 1, "first connect: reported once" );
	g_hostHasFactory = 1; g_factoryAccepts = 1;
	LoadMenuInterface();
	check( g_pMenu != NULL, "menu is picked up once it appears" );
	check( n_conDPrint == 1, "and no second message" );

	printf( "\n%s\n", fails ? "SOME MENU PROBE CHECKS FAILED" : "ALL MENU PROBE CHECKS PASSED" );
	return fails ? 1 : 0;
}
