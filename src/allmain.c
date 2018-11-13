/* NetHack 3.6	allmain.c	$NHDT-Date: 1446975459 2015/11/08 09:37:39 $  $NHDT-Branch: master $:$NHDT-Revision: 1.66 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

/* various code that was replicated in *main.c */


#include <zmq.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "hack.h"

#ifndef NO_SIGNAL
#include <signal.h>
#endif

#ifdef POSITIONBAR
STATIC_DCL void NDECL(do_positionbar);
#endif

void* requester;

void sendDieMsg()
{
	char* msg = "***died***";
    zmq_send (requester, msg, 11, 0);
	char buffer[1];	
    zmq_recv (requester, buffer, 1, 0);
}

char rcvDir()
{
	////FILE //f = fopen("log.txt", "a");
	//fprintf(f, "Sending ***dir***.\n");
	
	char* msg = "***dir***";
    zmq_send (requester, msg, 10, 0);
	
	//fprintf(f, "Sent - waiting for response\n");
    
	char buffer[1];	
    zmq_recv (requester, buffer, 1, 0);
	
	//fprintf(f, "Received dir: %c.\n", buffer[0]);
	//fclose(f);
	return buffer[0];
}

void
moveloop(resuming)
boolean resuming;
{
	char hostname[21];
	
	if (portnum == -1)
	{
		portnum = 5555;
	}
	//	FILE *of = fopen("conn.txt", "r");
	//	fgets(hostname, 21, of);
	//	fclose(of);
	//}
	//else
	//{ // add tcp:...etc
	sprintf(hostname, "tcp://localhost:%d", portnum);
	//}
	
    void *scontext = zmq_ctx_new ();
    requester = zmq_socket (scontext, ZMQ_REQ);
    zmq_connect (requester, hostname);
	
#if defined(MICRO) || defined(WIN32)
    char ch;
    int abort_lev;
#endif
    int moveamt = 0, wtcap = 0, change = 0;
    boolean monscanmove = FALSE;

    /* Note:  these initializers don't do anything except guarantee that
            we're linked properly.
    */
    decl_init();
    monst_init();
    monstr_init(); /* monster strengths */
    objects_init();

    if (wizard)
        add_debug_extended_commands();

    /* if a save file created in normal mode is now being restored in
       explore mode, treat it as normal restore followed by 'X' command
       to use up the save file and require confirmation for explore mode */
    if (resuming && iflags.deferred_X)
        (void) enter_explore_mode();

    /* side-effects from the real world */
    flags.moonphase = phase_of_the_moon();
    if (flags.moonphase == FULL_MOON) {
        You("are lucky!  Full moon tonight.");
        change_luck(1);
    } else if (flags.moonphase == NEW_MOON) {
        pline("Be careful!  New moon tonight.");
    }
    flags.friday13 = friday_13th();
    if (flags.friday13) {
        pline("Watch out!  Bad things can happen on Friday the 13th.");
        change_luck(-1);
    }

    if (!resuming) { /* new game */
        context.rndencode = rnd(9000);
        set_wear((struct obj *) 0); /* for side-effects of starting gear */
        (void) pickup(1);      /* autopickup at initial location */
    } else {                   /* restore old game */
#ifndef WIN32
        update_inventory(); /* for perm_invent */
#endif
        read_engr_at(u.ux, u.uy); /* subset of pickup() */
    }
#ifdef WIN32
    update_inventory(); /* for perm_invent */
#endif

    (void) encumber_msg(); /* in case they auto-picked up something */
    if (defer_see_monsters) {
        defer_see_monsters = FALSE;
        see_monsters();
    }
    initrack();

    u.uz0.dlevel = u.uz.dlevel;
    youmonst.movement = NORMAL_SPEED; /* give the hero some movement points */
    context.move = 0;
	
	int firstrun = 0;

    program_state.in_moveloop = 1;
		
    for (;;) {
		/*FILE *f = fopen("log.txt", "a");
		fprintf(f, "1\n");
		fclose(f);*/
#ifdef SAFERHANGUP
        if (program_state.done_hup)
            end_of_input();
#endif
        get_nh_event();
#ifdef POSITIONBAR
        do_positionbar();
#endif
		/*f = fopen("log.txt", "a");
		fprintf(f, "2\n");
		fclose(f);*/
        if (context.move) {
            /* actual time passed */
            youmonst.movement -= NORMAL_SPEED;

            do { /* hero can't move this turn loop */
                wtcap = encumber_msg();

                context.mon_moving = TRUE;
                do {
                    monscanmove = movemon();
                    if (youmonst.movement >= NORMAL_SPEED)
                        break; /* it's now your turn */
                } while (monscanmove);
                context.mon_moving = FALSE;

                if (!monscanmove && youmonst.movement < NORMAL_SPEED) {
                    /* both you and the monsters are out of steam this round
                     */
                    /* set up for a new turn */
                    struct monst *mtmp;
                    mcalcdistress(); /* adjust monsters' trap, blind, etc */

                    /* reallocate movement rations to monsters */
                    for (mtmp = fmon; mtmp; mtmp = mtmp->nmon)
                        mtmp->movement += mcalcmove(mtmp);

                    if (!rn2(u.uevent.udemigod
                                 ? 25
                                 : (depth(&u.uz) > depth(&stronghold_level))
                                       ? 50
                                       : 70))
                        (void) makemon((struct permonst *) 0, 0, 0,
                                       NO_MM_FLAGS);

                    /* calculate how much time passed. */
                    if (u.usteed && u.umoved) {
                        /* your speed doesn't augment steed's speed */
                        moveamt = mcalcmove(u.usteed);
                    } else {
                        moveamt = youmonst.data->mmove;

                        if (Very_fast) { /* speed boots or potion */
                            /* average movement is 1.67 times normal */
                            moveamt += NORMAL_SPEED / 2;
                            if (rn2(3) == 0)
                                moveamt += NORMAL_SPEED / 2;
                        } else if (Fast) {
                            /* average movement is 1.33 times normal */
                            if (rn2(3) != 0)
                                moveamt += NORMAL_SPEED / 2;
                        }
                    }

                    switch (wtcap) {
                    case UNENCUMBERED:
                        break;
                    case SLT_ENCUMBER:
                        moveamt -= (moveamt / 4);
                        break;
                    case MOD_ENCUMBER:
                        moveamt -= (moveamt / 2);
                        break;
                    case HVY_ENCUMBER:
                        moveamt -= ((moveamt * 3) / 4);
                        break;
                    case EXT_ENCUMBER:
                        moveamt -= ((moveamt * 7) / 8);
                        break;
                    default:
                        break;
                    }

                    youmonst.movement += moveamt;
                    if (youmonst.movement < 0)
                        youmonst.movement = 0;
                    settrack();

                    monstermoves++;
                    moves++;

                    /********************************/
                    /* once-per-turn things go here */
                    /********************************/

                    if (Glib)
                        glibr();
                    nh_timeout();
                    run_regions();

                    if (u.ublesscnt)
                        u.ublesscnt--;
                    if (flags.time && !context.run)
                        context.botl = 1;

                    /* One possible result of prayer is healing.  Whether or
                     * not you get healed depends on your current hit points.
                     * If you are allowed to regenerate during the prayer, the
                     * end-of-prayer calculation messes up on this.
                     * Another possible result is rehumanization, which
                     * requires
                     * that encumbrance and movement rate be recalculated.
                     */
                    if (u.uinvulnerable) {
                        /* for the moment at least, you're in tiptop shape */
                        wtcap = UNENCUMBERED;
                    } else if (Upolyd && youmonst.data->mlet == S_EEL
                               && !is_pool(u.ux, u.uy)
                               && !Is_waterlevel(&u.uz)) {
                        /* eel out of water loses hp, same as for monsters;
                           as hp gets lower, rate of further loss slows down
                           */
                        if (u.mh > 1 && rn2(u.mh) > rn2(8)
                            && (!Half_physical_damage || !(moves % 2L))) {
                            u.mh--;
                            context.botl = 1;
                        } else if (u.mh < 1)
                            rehumanize();
                    } else if (Upolyd && u.mh < u.mhmax) {
                        if (u.mh < 1)
                            rehumanize();
                        else if (Regeneration
                                 || (wtcap < MOD_ENCUMBER && !(moves % 20))) {
                            context.botl = 1;
                            u.mh++;
                        }
                    } else if (u.uhp < u.uhpmax
                               && (wtcap < MOD_ENCUMBER || !u.umoved
                                   || Regeneration)) {
                        if (u.ulevel > 9 && !(moves % 3)) {
                            int heal, Con = (int) ACURR(A_CON);

                            if (Con <= 12) {
                                heal = 1;
                            } else {
                                heal = rnd(Con);
                                if (heal > u.ulevel - 9)
                                    heal = u.ulevel - 9;
                            }
                            context.botl = 1;
                            u.uhp += heal;
                            if (u.uhp > u.uhpmax)
                                u.uhp = u.uhpmax;
                        } else if (Regeneration
                                   || (u.ulevel <= 9
                                       && !(moves
                                            % ((MAXULEV + 12) / (u.ulevel + 2)
                                               + 1)))) {
                            context.botl = 1;
                            u.uhp++;
                        }
                    }

                    /* moving around while encumbered is hard work */
                    if (wtcap > MOD_ENCUMBER && u.umoved) {
                        if (!(wtcap < EXT_ENCUMBER ? moves % 30
                                                   : moves % 10)) {
                            if (Upolyd && u.mh > 1) {
                                u.mh--;
                            } else if (!Upolyd && u.uhp > 1) {
                                u.uhp--;
                            } else {
                                You("pass out from exertion!");
                                exercise(A_CON, FALSE);
                                fall_asleep(-10, FALSE);
                            }
                        }
                    }

                    if ((u.uen < u.uenmax)
                        && ((wtcap < MOD_ENCUMBER
                             && (!(moves % ((MAXULEV + 8 - u.ulevel)
                                            * (Role_if(PM_WIZARD) ? 3 : 4)
                                            / 6)))) || Energy_regeneration)) {
                        u.uen += rn1(
                            (int) (ACURR(A_WIS) + ACURR(A_INT)) / 15 + 1, 1);
                        if (u.uen > u.uenmax)
                            u.uen = u.uenmax;
                        context.botl = 1;
                    }

                    if (!u.uinvulnerable) {
                        if (Teleportation && !rn2(85)) {
                            xchar old_ux = u.ux, old_uy = u.uy;
                            tele();
                            if (u.ux != old_ux || u.uy != old_uy) {
                                if (!next_to_u()) {
                                    check_leash(old_ux, old_uy);
                                }
                                /* clear doagain keystrokes */
                                pushch(0);
                                savech(0);
                            }
                        }
                        /* delayed change may not be valid anymore */
                        if ((change == 1 && !Polymorph)
                            || (change == 2 && u.ulycn == NON_PM))
                            change = 0;
                        if (Polymorph && !rn2(100))
                            change = 1;
                        else if (u.ulycn >= LOW_PM && !Upolyd
                                 && !rn2(80 - (20 * night())))
                            change = 2;
                        if (change && !Unchanging) {
                            if (multi >= 0) {
                                if (occupation)
                                    stop_occupation();
                                else
                                    nomul(0);
                                if (change == 1)
                                    polyself(0);
                                else
                                    you_were();
                                change = 0;
                            }
                        }
                    }

                    if (Searching && multi >= 0)
                        (void) dosearch0(1);
                    dosounds();
                    do_storms();
                    gethungry();
                    age_spells();
                    exerchk();
                    invault();
                    if (u.uhave.amulet)
                        amulet();
                    if (!rn2(40 + (int) (ACURR(A_DEX) * 3)))
                        u_wipe_engr(rnd(3));
                    if (u.uevent.udemigod && !u.uinvulnerable) {
                        if (u.udg_cnt)
                            u.udg_cnt--;
                        if (!u.udg_cnt) {
                            intervene();
                            u.udg_cnt = rn1(200, 50);
                        }
                    }
                    restore_attrib();
                    /* underwater and waterlevel vision are done here */
                    if (Is_waterlevel(&u.uz) || Is_airlevel(&u.uz))
                        movebubbles();
                    else if (Is_firelevel(&u.uz))
                        fumaroles();
                    else if (Underwater)
                        under_water(0);
                    /* vision while buried done here */
                    else if (u.uburied)
                        under_ground(0);

                    /* when immobile, count is in turns */
                    if (multi < 0) {
                        if (++multi == 0) { /* finished yet? */
                            unmul((char *) 0);
                            /* if unmul caused a level change, take it now */
                            if (u.utotype)
                                deferred_goto();
                        }
                    }
                }
            } while (youmonst.movement
                     < NORMAL_SPEED); /* hero can't move loop */

            /******************************************/
            /* once-per-hero-took-time things go here */
            /******************************************/

            if (context.bypasses)
                clear_bypasses();
            if ((u.uhave.amulet || Clairvoyant) && !In_endgame(&u.uz)
                && !BClairvoyant && !(moves % 15) && !rn2(2))
                do_vicinity_map();
            if (u.utrap && u.utraptype == TT_LAVA)
                sink_into_lava();
            /* when/if hero escapes from lava, he can't just stay there */
            else if (!u.umoved)
                (void) pooleffects(FALSE);

        } /* actual time passed */
		/*f = fopen("log.txt", "a");
		fprintf(f, "3\n");
		fclose(f);*/
        /****************************************/
        /* once-per-player-input things go here */
        /****************************************/

        clear_splitobjs();
        find_ac();
        if (!context.mv || Blind) {
            /* redo monsters if hallu or wearing a helm of telepathy */
            if (Hallucination) { /* update screen randomly */
                see_monsters();
                see_objects();
                see_traps();
                if (u.uswallow)
                    swallowed(0);
            } else if (Unblind_telepat) {
                see_monsters();
            } else if (Warning || Warn_of_mon)
                see_monsters();

            if (vision_full_recalc)
                vision_recalc(0); /* vision! */
        }
        if (context.botl || context.botlx) {
            bot();
            curs_on_u();
        }

        context.move = 1;
		/*f = fopen("log.txt", "a");
		fprintf(f, "4\n");
		fclose(f);*/
        if (multi >= 0 && occupation) {
#if defined(MICRO) || defined(WIN32)
            abort_lev = 0;
            if (kbhit()) {
                if ((ch = pgetchar()) == ABORT)
                    abort_lev++;
                else
                    pushch(ch);
            }
            if (!abort_lev && (*occupation)() == 0)
#else
            if ((*occupation)() == 0)
#endif
                occupation = 0;
            if (
#if defined(MICRO) || defined(WIN32)
                abort_lev ||
#endif
                monster_nearby()) {
                stop_occupation();
                reset_eat();
            }
#if defined(MICRO) || defined(WIN32)
            if (!(++occtime % 7))
                display_nhwindow(WIN_MAP, FALSE);
#endif
            continue;
        }
		
        if (iflags.sanity_check)
            sanity_check();

#ifdef CLIPPING
        /* just before rhack */
        cliparound(u.ux, u.uy);
#endif

        u.umoved = FALSE;
		
		/*f = fopen("log.txt", "a");
		fprintf(f, "5\n");
		fclose(f);*/
		
		char *rcv_cmd = malloc(sizeof(char)*1);
		*rcv_cmd = '\0';
		program_state.something_worth_saving = 0;
		
		if (firstrun == 0)
		{
			//(void) scrolltele((struct obj *) 0);
			// move player to upstair
		    /*
			u.dx = u.ux - xupstair; // xdir[dp - Cmd.dirchars];
		    u.dy = u.uy - yupstair; // ydir[dp - Cmd.dirchars];
		    u.dz = 0;
            context.mv = TRUE;
        	domove();*/
			
			if (flags.combat_setup)
			{
				int topleftx = u.ux;
				int toplefty = u.uy;

				while(1)
				{
					if (accessible(topleftx-1, toplefty))
						topleftx -= 1;
					else break;
				}
				while(1)
				{
					if (accessible(topleftx, toplefty-1))
						toplefty -= 1;
					else break;
				}
			
				int botrightx = u.ux;
				int botrighty = u.uy;
				while(1)
				{
					if (accessible(botrightx+1, botrighty))
						botrightx += 1;
					else break;
				}
				while(1)
				{
					if (accessible(botrightx, botrighty+1))
						botrighty += 1;
					else break;
				}
				
				u.ux = (topleftx + botrightx) / 2;
				u.uy = (toplefty + botrighty) / 2;
				
				// create monster
				(void) makemon(&mons[mtypeid], 0, 0, NO_MM_FLAGS);
			
				// increase player level to requested.
		        if (reqlevel > MAXULEV)
		            reqlevel = MAXULEV;
		        while (u.ulevel < reqlevel)
		            pluslvl(FALSE);
			
				// adjust str/dex as needed
				if (reqstr > 0)
					adjattrib(A_STR, reqstr, 1); // no message
				if (reqdex > 0)
					adjattrib(A_DEX, reqdex, 1);
				if (reqac != 999)
					u.uac = reqac;
				if (reqdlvl != 999)
					u.uz.dlevel = reqdlvl;
				if (reqlyc >= 0)
				{
					if (reqlyc == 0)
						u.ulycn = PM_WERERAT;
					else if (reqlyc == 1)
						u.ulycn = PM_WEREJACKAL;
					else if (reqlyc == 2)
						u.ulycn = PM_WEREWOLF;
				}
				if (reqstateffs > 1)
				{ // 1: stun, 2: conf, 3: blind, 4: hallu, 5-x: hunger/burden
					if (reqstateffs % 2 == 0)
					{
				        make_stunned((HStun & TIMEOUT) + (long) rn1(7, 16), FALSE);
					}
					if (reqstateffs % 3 == 0)
					{
						make_confused((HConfusion & TIMEOUT) + (long) rnd(100), FALSE);
					}
					if (reqstateffs % 5 == 0)
					{
						make_blinded((long) d(2, 10), FALSE);
					}
					if (reqstateffs % 7 == 0)
					{
				        make_hallucinated((HHallucination & TIMEOUT) + (long) rn1(5, 16), FALSE, 0L);
					}
				}
			}
			
			objects[POT_WATER].oc_name_known = 1;
        }
		
		/*f = fopen("log.txt", "a");
		fprintf(f, "6\n");
		fclose(f);*/
		
        for (struct obj *obj = invent; obj; obj = obj->nobj)
            if (not_fully_identified(obj))
                (void) identify(obj);
		
		if (flags.combat_setup)
			do_mapping();
		
		if (firstrun == 0)
		{
			if (nsdoor == 0)
			{
	            register int x, y;
	            for (x = 1; x < COLNO; x++)
	                for (y = 0; y < ROWNO; y++)
	                    if (levl[x][y].typ == SDOOR || levl[x][y].typ == SCORR)
							nsdoor ++;
				bot();
				//curs_on_u();
			}
			
			firstrun = 1;
			clear_topl();
		}
		if (flags.create_mons)
			look_all_n();
		
		int prev_num_expl = num_expl;
		num_expl = 0;
		int y,x;
		for (y = 0; y < ROWNO; y++) {
		    for (x = 1; x < COLNO; x++) {
				if (levl[x][y].typ != STONE && (levl[x][y].glyph >= GLYPH_CMAP_OFF && levl[x][y].glyph < GLYPH_EXPLODE_OFF && levl[x][y].glyph != cmap_to_glyph(S_stone)))
					num_expl ++;
			}
		}
		
		if (num_expl - prev_num_expl > 10)
		{
			bot();
			curs_on_u();
		}
		
		//bot();
		
		/*f = fopen("log.txt", "a");
		fprintf(f, "7\n");
		fclose(f);*/

		/* *** SEND MESSAGE TO PY SCRIPT *** */
		int numMonsters = monster_census(0);
		if (flags.combat_setup && numMonsters == 0)
		{ // monster dead
			char* msg = "***mondead***";
		    zmq_send (requester, msg, 14, 0);
		}
		else {
			char* map = malloc(COLNO*(ROWNO+9));
			memset(map, ' ', COLNO*(ROWNO+9));
		
			char* mapline = savemaptofile();
			int mp;
			for (mp = 0; mp < strlen(mapline); mp ++)
			{
				map[mp] = mapline[mp];
			}
					
			char* attline = getAttrs();

			int i;
			for (i = 0; i < strlen(attline); i ++)
			{
				map[(ROWNO*COLNO)+i] = attline[i];
			}
		
			char* sttline = getStats();		
			int j;
			for (j = 0; j < 80; j ++)
			{
				map[(ROWNO*COLNO)+i+j] = sttline[j];
			}
		
			char* topline = get_topl();
			map[(ROWNO*COLNO)+i+j+0] = '-';
			map[(ROWNO*COLNO)+i+j+1] = '-';
			int k;
			for (k = 0; k < strlen(topline); k ++)
			{
				map[(ROWNO*COLNO)+i+j+2+k] = topline[k];
			}
			free(topline);
			
			k ++;
			clear_topl();

			map[(ROWNO*COLNO)+i+j+2+k-1] = '*';
			map[(ROWNO*COLNO)+i+j+2+k] = '*';
		
			char intbuf[5];
			sprintf(intbuf, "%d", u.ux);
			map[(ROWNO*COLNO)+i+j+2+k+1] = intbuf[0];
			map[(ROWNO*COLNO)+i+j+2+k+2] = intbuf[1];
			map[(ROWNO*COLNO)+i+j+2+k+3] = '-';
			char intbuf2[5];
			sprintf(intbuf2, "%d", u.uy);
			map[(ROWNO*COLNO)+i+j+2+k+4] = intbuf2[0];
			map[(ROWNO*COLNO)+i+j+2+k+5] = intbuf2[1];
			map[(ROWNO*COLNO)+i+j+2+k+6] = '-';
		
			char intbuf3[5];
			sprintf(intbuf3, "%d", back_to_glyph(u.ux, u.uy));
			map[(ROWNO*COLNO)+i+j+2+k+7] = '(';
			map[(ROWNO*COLNO)+i+j+2+k+8] = intbuf3[0];
			map[(ROWNO*COLNO)+i+j+2+k+9] = intbuf3[1];
			map[(ROWNO*COLNO)+i+j+2+k+10] = intbuf3[2];
			map[(ROWNO*COLNO)+i+j+2+k+11] = intbuf3[3];
			map[(ROWNO*COLNO)+i+j+2+k+12] = ')';
		
			map[(ROWNO*COLNO)+i+j+2+k+13] = '\0';
			map[(ROWNO*COLNO)+i+j+2+k+14] = '\0';
			map = realloc(map, (ROWNO*COLNO)+i+j+2+k+14);

	        zmq_send (requester, map, (ROWNO*COLNO)+i+j+2+k+14, 0);
		}
		/* *** RCV COMMAND *** */
		
		/*f = fopen("log.txt", "a");
		fprintf(f, "8\n");
		fclose(f);*/
		
        char buffer[1];	
        zmq_recv (requester, buffer, 1, 0);
		rcv_cmd[0] = buffer[0];
		
		/*f = fopen("log.txt", "a");
		fprintf(f, "8.1: %s\n", rcv_cmd);
		fclose(f);*/
		
		if (rcv_cmd[0] == 'Q')
		{ // exit
			end_of_input();
			break;
		}
		else if (rcv_cmd[0] == '~')
		{
			char* invenbuf = malloc(2048);
			(void) display_inventory((char *) 0, FALSE, &invenbuf[0]);
			zmq_send (requester, invenbuf, 2048, 0);

			rcv_cmd[0] = 'i';
	        context.move = FALSE;
	        multi = 0;
		}
		
		/*if (rcv_cmd[0] == 'M')
		{
			fprintf(f, "Received request for full map.");
            register int x, y;

			// from read.c, case SCR_MAGIC_MAPPING
			do_mapping(); // reveals secret passages,but not doors
			
			rcv_cmd[0] = '.'; // change to wait
		}*/
		
		resetMore();
	
		u.uhunger = 900;
		
		/*f = fopen("log.txt", "a");
		fprintf(f, "8.2\n");
		fclose(f);*/
		
        if (multi > 0) {
            lookaround();
            if (!multi) {
                /* lookaround may clear multi */
                context.move = 0;
                if (flags.time)
                    context.botl = 1;
                continue;
            }
            if (context.mv) {
                if (multi < COLNO && !--multi)
                    context.travel = context.travel1 = context.mv =
                        context.run = 0;
                domove();
            } else {
                --multi;
				rhack(save_cm);
            }
        }
		else if (multi == 0) {
#ifdef MAIL
            ckmailstatus();
#endif
			if (*rcv_cmd == '\0')
			{
				rhack((char *) 0);
			}
			else
				rhack(rcv_cmd);
        }
		/*f = fopen("log.txt", "a");
		fprintf(f, "8.5\n");
		fclose(f);*/
        if (u.utotype)       /* change dungeon level */
            deferred_goto(); /* after rhack() */
        /* !context.move here: multiple movement command stopped */
        else if (flags.time && (!context.move || !context.mv))
            context.botl = 1;
		/*f = fopen("log.txt", "a");
		fprintf(f, "8.6\n");
		fclose(f);*/
        if (vision_full_recalc)
            vision_recalc(0); /* vision! */
        /* when running in non-tport mode, this gets done through domove() */
        if ((!context.run || flags.runmode == RUN_TPORT)
            && (multi && (!context.travel ? !(multi % 7) : !(moves % 7L)))) {
            if (flags.time && context.run)
                context.botl = 1;
            display_nhwindow(WIN_MAP, FALSE);
        }
		/*f = fopen("log.txt", "a");
		fprintf(f, "9\n");
		fclose(f);*/
    }
	
    zmq_close (requester);
    zmq_ctx_destroy (scontext);
}

void
stop_occupation()
{
    if (occupation) {
        if (!maybe_finished_meal(TRUE))
            You("stop %s.", occtxt);
        occupation = 0;
        context.botl = 1; /* in case u.uhs changed */
        nomul(0);
        pushch(0);
    }
}

void
display_gamewindows()
{
    WIN_MESSAGE = create_nhwindow(NHW_MESSAGE);
#ifdef STATUS_VIA_WINDOWPORT
    status_initialize(0);
#else
    WIN_STATUS = create_nhwindow(NHW_STATUS);
#endif
    WIN_MAP = create_nhwindow(NHW_MAP);
    WIN_INVEN = create_nhwindow(NHW_MENU);

#ifdef MAC
    /* This _is_ the right place for this - maybe we will
     * have to split display_gamewindows into create_gamewindows
     * and show_gamewindows to get rid of this ifdef...
     */
    if (!strcmp(windowprocs.name, "mac"))
        SanePositions();
#endif

    /*
     * The mac port is not DEPENDENT on the order of these
     * displays, but it looks a lot better this way...
     */
#ifndef STATUS_VIA_WINDOWPORT
    display_nhwindow(WIN_STATUS, FALSE);
#endif
    display_nhwindow(WIN_MESSAGE, FALSE);
    clear_glyph_buffer();
    display_nhwindow(WIN_MAP, FALSE);	
}

void
newgame()
{
    int i;

#ifdef MFLOPPY
    gameDiskPrompt();
#endif

	//FILE *f = fopen("log.txt", "w");
	//fclose(f);
	
    context.botlx = 1;
    context.ident = 1;
    context.stethoscope_move = -1L;
    context.warnlevel = 1;
    context.next_attrib_check = 600L; /* arbitrary first setting */
    context.tribute.enabled = TRUE;   /* turn on 3.6 tributes    */
    context.tribute.tributesz = sizeof(struct tribute_info);

    for (i = 0; i < NUMMONS; i++)
        mvitals[i].mvflags = mons[i].geno & G_NOCORPSE;

    init_objects(); /* must be before u_init() */

    flags.pantheon = -1; /* role_init() will reset this */
    role_init();         /* must be before init_dungeons(), u_init(),
                          * and init_artifacts() */

    init_dungeons();  /* must be before u_init() to avoid rndmonst()
                       * creating odd monsters for any tins and eggs
                       * in hero's initial inventory */
    init_artifacts(); /* before u_init() in case $WIZKIT specifies
                       * any artifacts */
    u_init();

#ifndef NO_SIGNAL
    (void) signal(SIGINT, (SIG_RET_TYPE) done1);
#endif
#ifdef NEWS
    if (iflags.news)
        display_file(NEWS, FALSE);
#endif
    load_qtlist();          /* load up the quest text info */
    /* quest_init();  --  Now part of role_init() */

    mklev();
    if (flags.combat_setup)
		u_on_rndspot(1);
	else
		u_on_upstairs();
	if (wizard)
        obj_delivery(FALSE); /* finish wizkit */
    vision_reset();          /* set up internals for level (after mklev) */
    check_special_room(FALSE);

    if (MON_AT(u.ux, u.uy))
        mnexto(m_at(u.ux, u.uy));
    (void) makedog();
    docrt();

    if (flags.legacy) {
        flush_screen(1);
  //      com_pager(1);
    }

#ifdef INSURANCE
    save_currentstate();
#endif
    program_state.something_worth_saving++; /* useful data now exists */

    urealtime.realtime = 0L;
#if defined(BSD) && !defined(POSIX_TYPES)
    (void) time((long *) &urealtime.restored);
#else
    (void) time(&urealtime.restored);
#endif

    /* Success! */
    //welcome(TRUE);
    return;
}

/* show "welcome [back] to nethack" message at program startup */
void
welcome(new_game)
boolean new_game; /* false => restoring an old game */
{
    char buf[BUFSZ];
    boolean currentgend = Upolyd ? u.mfemale : flags.female;

    /*
     * The "welcome back" message always describes your innate form
     * even when polymorphed or wearing a helm of opposite alignment.
     * Alignment is shown unconditionally for new games; for restores
     * it's only shown if it has changed from its original value.
     * Sex is shown for new games except when it is redundant; for
     * restores it's only shown if different from its original value.
     */
    *buf = '\0';
    if (new_game || u.ualignbase[A_ORIGINAL] != u.ualignbase[A_CURRENT])
        Sprintf(eos(buf), " %s", align_str(u.ualignbase[A_ORIGINAL]));
    if (!urole.name.f
        && (new_game
                ? (urole.allow & ROLE_GENDMASK) == (ROLE_MALE | ROLE_FEMALE)
                : currentgend != flags.initgend))
        Sprintf(eos(buf), " %s", genders[currentgend].adj);

    pline(new_game ? "%s %s, welcome to NetHack!  You are a%s %s %s."
                   : "%s %s, the%s %s %s, welcome back to NetHack!",
          Hello((struct monst *) 0), plname, buf, urace.adj,
          (currentgend && urole.name.f) ? urole.name.f : urole.name.m);
}

#ifdef POSITIONBAR
STATIC_DCL void
do_positionbar()
{
    static char pbar[COLNO];
    char *p;

    p = pbar;
    /* up stairway */
    if (upstair.sx
        && (glyph_to_cmap(level.locations[upstair.sx][upstair.sy].glyph)
                == S_upstair
            || glyph_to_cmap(level.locations[upstair.sx][upstair.sy].glyph)
                   == S_upladder)) {
        *p++ = '<';
        *p++ = upstair.sx;
    }
    if (sstairs.sx
        && (glyph_to_cmap(level.locations[sstairs.sx][sstairs.sy].glyph)
                == S_upstair
            || glyph_to_cmap(level.locations[sstairs.sx][sstairs.sy].glyph)
                   == S_upladder)) {
        *p++ = '<';
        *p++ = sstairs.sx;
    }

    /* down stairway */
    if (dnstair.sx
        && (glyph_to_cmap(level.locations[dnstair.sx][dnstair.sy].glyph)
                == S_dnstair
            || glyph_to_cmap(level.locations[dnstair.sx][dnstair.sy].glyph)
                   == S_dnladder)) {
        *p++ = '>';
        *p++ = dnstair.sx;
    }
    if (sstairs.sx
        && (glyph_to_cmap(level.locations[sstairs.sx][sstairs.sy].glyph)
                == S_dnstair
            || glyph_to_cmap(level.locations[sstairs.sx][sstairs.sy].glyph)
                   == S_dnladder)) {
        *p++ = '>';
        *p++ = sstairs.sx;
    }

    /* hero location */
    if (u.ux) {
        *p++ = '@';
        *p++ = u.ux;
    }
    /* fence post */
    *p = 0;

    update_positionbar(pbar);
}
#endif

/*allmain.c*/
