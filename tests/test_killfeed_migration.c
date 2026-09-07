/* Migration test: archived killfeed cvars must move to new defaults once,
 * and a user tweak must survive the migration.
 * Mirrors the block in HUD_Init (death.cpp) -- kept in sync by
 * scripts/kf_migration_check.py. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* --- minimal fake engine --------------------------------------------- */
#define MAXCV 32
typedef struct { char key[64]; char val[64]; float value; const char *string; } cvar_t;

static cvar_t g_cv[MAXCV];
static int g_ncv;
static int g_sets;

static cvar_t *fake_find( const char *k )
{
	for( int i = 0; i < g_ncv; i++ )
		if( !strcmp( g_cv[i].key, k ) ) return &g_cv[i];
	return NULL;
}

static void fake_set( const char *k, const char *v )
{
	cvar_t *c = fake_find( k );
	if( !c ) { snprintf( g_cv[g_ncv].key, 64, "%s", k ); c = &g_cv[g_ncv++]; }
	snprintf( c->val, 64, "%s", v );
	c->string = c->val;
	c->value = (float)atof( v );
	g_sets++;
}

#define cv_set( k, v ) fake_set( k, v )

/* the migration block, copied verbatim from death.cpp except for the
 * engine accessors */
static void run_migration( void )
{
	static const char *const KF_DEFAULT_KEYS[] = {
		"cl_killfeed_scale", "cl_killfeed_plate_color", "cl_killfeed_plate_alpha",
		"cl_killfeed_ct_color", "cl_killfeed_t_color"
	};
	static const char *const KF_PREV_DEFAULTS[] = {
		"1.4", "46 43 42", "136", "129 154 202", "221 195 135"
	};
	static const char *const KF_NEW_DEFAULTS[] = {
		"1.55", "14 14 14", "179", "131 165 222", "232 197 111"
	};
	cvar_t *ver = fake_find( "cl_killfeed_cfgversion" );
	if( ver->value < 2.0f )
	{
		for( size_t k = 0; k < sizeof( KF_DEFAULT_KEYS ) / sizeof( KF_DEFAULT_KEYS[0] ); k++ )
		{
			cvar_t *c = fake_find( KF_DEFAULT_KEYS[k] );
			if( !c ) continue;
			if( !strcmp( c->string, KF_PREV_DEFAULTS[k] ) )
				fake_set( KF_DEFAULT_KEYS[k], KF_NEW_DEFAULTS[k] );
		}
		fake_set( "cl_killfeed_cfgversion", "2" );
	}
}

int main( void )
{
	/* case 1: stale defaults from an old config -> must migrate */
	g_ncv = 0; g_sets = 0;
	cv_set( "cl_killfeed_scale", "1.4" );
	cv_set( "cl_killfeed_plate_color", "46 43 42" );
	cv_set( "cl_killfeed_plate_alpha", "136" );
	cv_set( "cl_killfeed_ct_color", "129 154 202" );
	cv_set( "cl_killfeed_t_color", "221 195 135" );
	cv_set( "cl_killfeed_cfgversion", "0" );
	run_migration();
	assert( !strcmp( fake_find( "cl_killfeed_scale" )->string, "1.55" ));
	assert( !strcmp( fake_find( "cl_killfeed_plate_color" )->string, "14 14 14" ));
	assert( !strcmp( fake_find( "cl_killfeed_plate_alpha" )->string, "179" ));
	assert( !strcmp( fake_find( "cl_killfeed_ct_color" )->string, "131 165 222" ));
	assert( !strcmp( fake_find( "cl_killfeed_t_color" )->string, "232 197 111" ));
	assert( !strcmp( fake_find( "cl_killfeed_cfgversion" )->string, "2" ));
	printf( "case1 stale-config migration: OK\n" );

	/* case 2: user tweak survives (value != previous default) */
	g_ncv = 0; g_sets = 0;
	cv_set( "cl_killfeed_scale", "2.0" );          /* user set this */
	cv_set( "cl_killfeed_plate_color", "46 43 42" );
	cv_set( "cl_killfeed_cfgversion", "0" );
	run_migration();
	assert( !strcmp( fake_find( "cl_killfeed_scale" )->string, "2.0" ));
	assert( !strcmp( fake_find( "cl_killfeed_plate_color" )->string, "14 14 14" ));
	printf( "case2 user tweak survives: OK\n" );

	/* case 3: already migrated -> no second pass, no writes */
	g_ncv = 0; g_sets = 0;
	cv_set( "cl_killfeed_scale", "1.55" );
	cv_set( "cl_killfeed_cfgversion", "2" );
	g_sets = 0;   /* подготовка не считается: меряем только саму миграцию */
	run_migration();
	assert( g_sets == 0 && "no writes on second run" );
	printf( "case3 idempotent: OK (sets=%d)\n", g_sets );

	/* case 4: fresh install (defaults from code, version still 0 because the
	 * cvar was just created) -> nothing to do, defaults already new */
	g_ncv = 0; g_sets = 0;
	cv_set( "cl_killfeed_scale", "1.55" );
	cv_set( "cl_killfeed_plate_color", "14 14 14" );
	cv_set( "cl_killfeed_cfgversion", "0" );
	g_sets = 0;
	run_migration();
	assert( g_sets == 1 && "only the version stamp" );
	printf( "case4 fresh install: OK\n" );

	printf( "ALL MIGRATION CHECKS PASSED\n" );
	return 0;
}
