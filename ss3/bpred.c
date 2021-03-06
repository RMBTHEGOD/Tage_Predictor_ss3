/* bpred.c - branch predictor routines */

/* SimpleScalar(TM) Tool Suite
 * Copyright (C) 1994-2003 by Todd M. Austin, Ph.D. and SimpleScalar, LLC.
 * All Rights Reserved. 
 * 
 * THIS IS A LEGAL DOCUMENT, BY USING SIMPLESCALAR,
 * YOU ARE AGREEING TO THESE TERMS AND CONDITIONS.
 * 
 * No portion of this work may be used by any commercial entity, or for any
 * commercial purpose, without the prior, written permission of SimpleScalar,
 * LLC (info@simplescalar.com). Nonprofit and noncommercial use is permitted
 * as described below.
 * 
 * 1. SimpleScalar is provided AS IS, with no warranty of any kind, express
 * or implied. The user of the program accepts full responsibility for the
 * application of the program and the use of any results.
 * 
 * 2. Nonprofit and noncommercial use is encouraged. SimpleScalar may be
 * downloaded, compiled, executed, copied, and modified solely for nonprofit,
 * educational, noncommercial research, and noncommercial scholarship
 * purposes provided that this notice in its entirety accompanies all copies.
 * Copies of the modified software can be delivered to persons who use it
 * solely for nonprofit, educational, noncommercial research, and
 * noncommercial scholarship purposes provided that this notice in its
 * entirety accompanies all copies.
 * 
 * 3. ALL COMMERCIAL USE, AND ALL USE BY FOR PROFIT ENTITIES, IS EXPRESSLY
 * PROHIBITED WITHOUT A LICENSE FROM SIMPLESCALAR, LLC (info@simplescalar.com).
 * 
 * 4. No nonprofit user may place any restrictions on the use of this software,
 * including as modified by the user, by any other authorized user.
 * 
 * 5. Noncommercial and nonprofit users may distribute copies of SimpleScalar
 * in compiled or executable form as set forth in Section 2, provided that
 * either: (A) it is accompanied by the corresponding machine-readable source
 * code, or (B) it is accompanied by a written offer, with no time limit, to
 * give anyone a machine-readable copy of the corresponding source code in
 * return for reimbursement of the cost of distribution. This written offer
 * must permit verbatim duplication by anyone, or (C) it is distributed by
 * someone who received only the executable form, and is accompanied by a
 * copy of the written offer of source code.
 * 
 * 6. SimpleScalar was developed by Todd M. Austin, Ph.D. The tool suite is
 * currently maintained by SimpleScalar LLC (info@simplescalar.com). US Mail:
 * 2395 Timbercrest Court, Ann Arbor, MI 48105.
 * 
 * Copyright (C) 1994-2003 by Todd M. Austin, Ph.D. and SimpleScalar, LLC.
 */


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "bpred.h"


/* turn this on to enable the SimpleScalar 2.0 RAS bug */
/* #define RAS_BUG_COMPATIBLE */

/* create a branch predictor */
struct bpred_t *			/* branch predictory instance */
bpred_create(enum bpred_class class,	/* type of predictor to create */
	     unsigned int bimod_size,	/* bimod table size */
	     unsigned int l1size,	/* 2lev l1 table size */
	     unsigned int l2size,	/* 2lev l2 table size */
	     unsigned int meta_size,	/* meta table size */
	     unsigned int shift_width,	/* history register width */
	     unsigned int xor,  	/* history xor address flag */
	     unsigned int btb_sets,	/* number of sets in BTB */ 
	     unsigned int btb_assoc,	/* BTB associativity */
	     unsigned int retstack_size) /* num entries in ret-addr stack */
{
  struct bpred_t *pred;

  if (!(pred = calloc(1, sizeof(struct bpred_t))))
    fatal("out of virtual memory");

  pred->class = class;

  switch (class) {
  case BPredComb:
    /* bimodal component */
    pred->dirpred.bimod = 
      bpred_dir_create(BPred2bit, bimod_size, 0, 0, 0);

    /* 2-level component */
    pred->dirpred.twolev = 
      bpred_dir_create(BPred2Level, l1size, l2size, shift_width, xor);

    /* metapredictor component */
    pred->dirpred.meta = 
	    bpred_dir_create(BPred2bit, meta_size, 0, 0, 0);

    break;

  case BPredTage:
    pred->dirpred.tage = bpred_dir_create(class, /*T1 Size*/l1size, /*T2 Size*/l2size, /*T3 Size*/shift_width,/*T4 Size*/xor);
    if(!(pred->dirpred.tage->config.tage.bimod=calloc(bimod_size,sizeof(struct bimod_predictor))))
    {
	    fatal("Cannot allocated memory for bimodal predictor");
    }
    pred->dirpred.tage->config.tage.size=bimod_size;
    break;
  case BPred2Level:
    pred->dirpred.twolev = 
	    bpred_dir_create(class, l1size, l2size, shift_width, xor);

    break;

  case BPred2bit:
    pred->dirpred.bimod =  
	    bpred_dir_create(class, bimod_size, 0, 0, 0);

  case BPredTaken:
  case BPredNotTaken:
    /* no other state */
    break;

  default:
    panic("bogus predictor class");
  }

  /* allocate ret-addr stack */
  switch (class) {
	  case BPredComb:
	  case BPred2Level:
	  case BPredTage:
	  case BPred2bit:
		  {
			  int i;

			  /* allocate BTB */
			  if (!btb_sets || (btb_sets & (btb_sets-1)) != 0)
				  fatal("number of BTB sets must be non-zero and a power of two");
			  if (!btb_assoc || (btb_assoc & (btb_assoc-1)) != 0)
				  fatal("BTB associativity must be non-zero and a power of two");

			  if (!(pred->btb.btb_data = calloc(btb_sets * btb_assoc,
							  sizeof(struct bpred_btb_ent_t))))
				  fatal("cannot allocate BTB");

			  pred->btb.sets = btb_sets;
			  pred->btb.assoc = btb_assoc;

			  if (pred->btb.assoc > 1)
				  for (i=0; i < (pred->btb.assoc*pred->btb.sets); i++)
				  {
					  if (i % pred->btb.assoc != pred->btb.assoc - 1)
						  pred->btb.btb_data[i].next = &pred->btb.btb_data[i+1];
					  else
						  pred->btb.btb_data[i].next = NULL;

					  if (i % pred->btb.assoc != pred->btb.assoc - 1)
						  pred->btb.btb_data[i+1].prev = &pred->btb.btb_data[i];
				  }

			  /* allocate retstack */
			  if ((retstack_size & (retstack_size-1)) != 0)
				  fatal("Return-address-stack size must be zero or a power of two");

			  pred->retstack.size = retstack_size;
			  if (retstack_size)
				  if (!(pred->retstack.stack = calloc(retstack_size, 
								  sizeof(struct bpred_btb_ent_t))))
					  fatal("cannot allocate return-address-stack");
			  pred->retstack.tos = retstack_size - 1;

			  break;
		  }

	  case BPredTaken:
	  case BPredNotTaken:
		  /* no other state */
		  break;
	  default:
		  panic("bogus predictor class");
  }

  return pred;
}

/* create a branch direction predictor */
	struct bpred_dir_t *		/* branch direction predictor instance */
bpred_dir_create (
		enum bpred_class class,	/* type of predictor to create */
		unsigned int l1size,	 	/* level-1 table size */
		unsigned int l2size,	 	/* level-2 table size (if relevant) */
		unsigned int shift_width,	/* history register width */
		unsigned int xor)	    	/* history xor address flag */
{
	struct bpred_dir_t *pred_dir;
	unsigned int cnt;
	int flipflop;

	if (!(pred_dir = calloc(1, sizeof(struct bpred_dir_t))))
		fatal("out of virtual memory");

	pred_dir->class = class;
	cnt = -1;
	switch (class) {
		case BPredTage:
			if (!l1size || (l1size & (l1size-1)) != 0)
				fatal("T-1 size, `%d', must be non-zero and a power of two", 
						l1size);
			pred_dir->config.tage.t1size = l1size;

			if (!l2size || (l2size & (l2size-1)) != 0)
				fatal("T-2 size, `%d', must be non-zero and a power of two", 
						l2size);
			pred_dir->config.tage.t2size = l2size;

			if (!shift_width || (shift_width & (shift_width-1)) != 0)
				fatal("T-3 size, `%d', must be non-zero and a power of two", 
						shift_width);
			pred_dir->config.tage.t3size = shift_width;
			if (!xor || (xor & (xor-1)) != 0)
				fatal("T-4 size, `%d', must be non-zero and a power of two", 
						xor);
			pred_dir->config.tage.t4size = xor;
			pred_dir->config.tage.clock=0;
			pred_dir->config.tage.clock_flip=1;
			pred_dir->config.tage.primePred=-1;
			pred_dir->config.tage.altPred=-1;
			pred_dir->config.tage.primeTagComp=NUMBEROFTAGTABLE;
			pred_dir->config.tage.altTagComp=NUMBEROFTAGTABLE;
			if(!(pred_dir->config.tage.geometric_lengths=calloc(NUMBEROFTAGTABLE,sizeof(int))))
			{
				fatal("cannot allocate geometric_lengths ");
			}
			pred_dir->config.tage.geometric_lengths[0]=GEOMETRICLENGTH0;
			pred_dir->config.tage.geometric_lengths[1]=GEOMETRICLENGTH1;
			pred_dir->config.tage.geometric_lengths[2]=GEOMETRICLENGTH2;
			pred_dir->config.tage.geometric_lengths[3]=GEOMETRICLENGTH3;
			if(!(pred_dir->config.tage.tag_size=calloc(NUMBEROFTAGTABLE,sizeof(int))))
			{
				fatal("cannot allocate geometric_lengths ");
			}
			pred_dir->config.tage.tag_size[0]=TAG0;
			pred_dir->config.tage.tag_size[1]=TAG1;
			pred_dir->config.tage.tag_size[2]=TAG2;
			pred_dir->config.tage.tag_size[3]=TAG3;
			for(int i=0;i<NUMBEROFTAGTABLE;i++)
			{
				if(!(pred_dir->config.tage.tag_comp_entry[i]=calloc(l1size,sizeof(struct tag_comp_entry))))
				{
					fatal("cannot allocate tage_comp_table ");
				}
				for(int j=0;j<l1size;j++)
				{
					pred_dir->config.tage.tag_comp_entry[i][j].tag=-1;
				}
			}
			if(!(pred_dir->config.tage.folded_history_index=calloc(NUMBEROFTAGTABLE,sizeof(struct folded_history))))
			{
				fatal("cannot allocate folded_history_index ");
			}
			for(int i=0;i<2;i++)
				if(!(pred_dir->config.tage.folded_history_tag[i]=calloc(NUMBEROFTAGTABLE,sizeof(struct folded_history))))
				{
					fatal("cannot allocate folded_history_tag for table ");
				}
			for(int i=0;i<NUMBEROFTAGTABLE;i++)
			{
				pred_dir->config.tage.folded_history_index[i].folded_length=(int)(log2(l1size));
				pred_dir->config.tage.folded_history_tag[0][i].folded_length=pred_dir->config.tage.tag_size[i];
				pred_dir->config.tage.folded_history_tag[1][i].folded_length=pred_dir->config.tage.tag_size[i]-1;


			}
			if(!(pred_dir->config.tage.geometric_history=calloc(pred_dir->config.tage.geometric_lengths[0]+1,sizeof(unsigned int))))
			{
				fatal("cannot allocate geometric_lengths ");
			}
			if(!(pred_dir->config.tage.tag_comp_index=calloc(NUMBEROFTAGTABLE,sizeof(int))))
			{
				fatal("cannot allocate tag_comp_index");
			}
			if(!(pred_dir->config.tage.tag_comp_tag=calloc(NUMBEROFTAGTABLE,sizeof(int))))
			{
				fatal("cannot allocate tag_comp_tag");
			}

			break;

		case BPred2Level:
			{
				if (!l1size || (l1size & (l1size-1)) != 0)
					fatal("level-1 size, `%d', must be non-zero and a power of two", 
							l1size);
				pred_dir->config.two.l1size = l1size;

				if (!l2size || (l2size & (l2size-1)) != 0)
					fatal("level-2 size, `%d', must be non-zero and a power of two", 
							l2size);
				pred_dir->config.two.l2size = l2size;

				if (!shift_width || shift_width > 30)
					fatal("shift register width, `%d', must be non-zero and positive",
							shift_width);
				pred_dir->config.two.shift_width = shift_width;
				pred_dir->config.two.xor = xor;
				pred_dir->config.two.shiftregs = calloc(l1size, sizeof(int));
				if (!pred_dir->config.two.shiftregs)
					fatal("cannot allocate shift register table");

				pred_dir->config.two.l2table = calloc(l2size, sizeof(unsigned char));
				if (!pred_dir->config.two.l2table)
					fatal("cannot allocate second level table");

				/* initialize counters to weakly this-or-that */
				flipflop = 1;
				for (cnt = 0; cnt < l2size; cnt++)
				{
					pred_dir->config.two.l2table[cnt] = flipflop;
					flipflop = 3 - flipflop;
				}

				break;
			}

		case BPred2bit:
			if (!l1size || (l1size & (l1size-1)) != 0)
				fatal("2bit table size, `%d', must be non-zero and a power of two", 
						l1size);
			pred_dir->config.bimod.size = l1size;
			if (!(pred_dir->config.bimod.table =
						calloc(l1size, sizeof(unsigned char))))
				fatal("cannot allocate 2bit storage");
			/* initialize counters to weakly this-or-that */
			flipflop = 1;
			for (cnt = 0; cnt < l1size; cnt++)
			{
				pred_dir->config.bimod.table[cnt] = flipflop;
				flipflop = 3 - flipflop;
			}

			break;

		case BPredTaken:
		case BPredNotTaken:
			/* no other state */
			break;

		default:
			panic("bogus branch direction predictor class");
	}

	return pred_dir;
}

/* print branch direction predictor configuration */
	void
bpred_dir_config(
		struct bpred_dir_t *pred_dir,	/* branch direction predictor instance */
		char name[],			/* predictor name */
		FILE *stream)			/* output stream */
{
	switch (pred_dir->class) {
		case BPredTage:
			fprintf(stream,
					"pred_dir: %s: tage: %d T1-sz, %d T2-sz, %d T3-sz, %d T4-sz, direct-mapped\n",
					name, pred_dir->config.tage.t1size, pred_dir->config.tage.t2size,
					pred_dir->config.tage.t3size, pred_dir->config.tage.t4size);
			break;
		case BPred2Level:
			fprintf(stream,
					"pred_dir: %s: 2-lvl: %d l1-sz, %d bits/ent, %s xor, %d l2-sz, direct-mapped\n",
					name, pred_dir->config.two.l1size, pred_dir->config.two.shift_width,
					pred_dir->config.two.xor ? "" : "no", pred_dir->config.two.l2size);
			break;
		case BPred2bit:
			fprintf(stream, "pred_dir: %s: 2-bit: %d entries, direct-mapped\n",
					name, pred_dir->config.bimod.size);
			break;

		case BPredTaken:
			fprintf(stream, "pred_dir: %s: predict taken\n", name);
			break;

		case BPredNotTaken:
			fprintf(stream, "pred_dir: %s: predict not taken\n", name);
			break;

		default:
			panic("bogus branch direction predictor class");
	}
}

/* print branch predictor configuration */
	void
bpred_config(struct bpred_t *pred,	/* branch predictor instance */
		FILE *stream)		/* output stream */
{
	switch (pred->class) {
		case BPredComb:
			bpred_dir_config (pred->dirpred.bimod, "bimod", stream);
			bpred_dir_config (pred->dirpred.twolev, "2lev", stream);
			bpred_dir_config (pred->dirpred.meta, "meta", stream);
			fprintf(stream, "btb: %d sets x %d associativity", 
					pred->btb.sets, pred->btb.assoc);
			fprintf(stream, "ret_stack: %d entries", pred->retstack.size);
			break;
		case BPredTage:
			bpred_dir_config (pred->dirpred.tage, "tage", stream);
			fprintf(stream, "btb: %d sets x %d associativity", 
					pred->btb.sets, pred->btb.assoc);
			fprintf(stream, "ret_stack: %d entries", pred->retstack.size);
			break;
		case BPred2Level:
			bpred_dir_config (pred->dirpred.twolev, "2lev", stream);
			fprintf(stream, "btb: %d sets x %d associativity", 
					pred->btb.sets, pred->btb.assoc);
			fprintf(stream, "ret_stack: %d entries", pred->retstack.size);
			break;

		case BPred2bit:
			bpred_dir_config (pred->dirpred.bimod, "bimod", stream);
			fprintf(stream, "btb: %d sets x %d associativity", 
					pred->btb.sets, pred->btb.assoc);
			fprintf(stream, "ret_stack: %d entries", pred->retstack.size);
			break;

		case BPredTaken:
			bpred_dir_config (pred->dirpred.bimod, "taken", stream);
			break;
		case BPredNotTaken:
			bpred_dir_config (pred->dirpred.bimod, "nottaken", stream);
			break;

		default:
			panic("bogus branch predictor class");
	}
}

/* print predictor stats */
	void
bpred_stats(struct bpred_t *pred,	/* branch predictor instance */
		FILE *stream)		/* output stream */
{
	fprintf(stream, "pred: addr-prediction rate = %f\n",
			(double)pred->addr_hits/(double)(pred->addr_hits+pred->misses));
	fprintf(stream, "pred: dir-prediction rate = %f\n",
			(double)pred->dir_hits/(double)(pred->dir_hits+pred->misses));
}

/* register branch predictor stats */
	void
bpred_reg_stats(struct bpred_t *pred,	/* branch predictor instance */
		struct stat_sdb_t *sdb)	/* stats database */
{
	char buf[512], buf1[512], *name;

	/* get a name for this predictor */
	switch (pred->class)
	{
		case BPredComb:
			name = "bpred_comb";
		case BPredTage:
			name = "bpred_tage";
			break;
		case BPred2Level:
			name = "bpred_2lev";
			break;
		case BPred2bit:
			name = "bpred_bimod";
			break;
		case BPredTaken:
			name = "bpred_taken";
			break;
		case BPredNotTaken:
			name = "bpred_nottaken";
			break;
		default:
			panic("bogus branch predictor class");
	}

	sprintf(buf, "%s.lookups", name);
	stat_reg_counter(sdb, buf, "total number of bpred lookups",
			&pred->lookups, 0, NULL);
	sprintf(buf, "%s.updates", name);
	sprintf(buf1, "%s.dir_hits + %s.misses", name, name);
	stat_reg_formula(sdb, buf, "total number of updates", buf1, "%12.0f");
	sprintf(buf, "%s.addr_hits", name);
	stat_reg_counter(sdb, buf, "total number of address-predicted hits", 
			&pred->addr_hits, 0, NULL);
	sprintf(buf, "%s.dir_hits", name);
	stat_reg_counter(sdb, buf, 
			"total number of direction-predicted hits "
			"(includes addr-hits)", 
			&pred->dir_hits, 0, NULL);
	if (pred->class == BPredComb)
	{
		sprintf(buf, "%s.used_bimod", name);
		stat_reg_counter(sdb, buf, 
				"total number of bimodal predictions used", 
				&pred->used_bimod, 0, NULL);
		sprintf(buf, "%s.used_2lev", name);
		stat_reg_counter(sdb, buf, 
				"total number of 2-level predictions used", 
				&pred->used_2lev, 0, NULL);
	}
	sprintf(buf, "%s.misses", name);
	stat_reg_counter(sdb, buf, "total number of misses", &pred->misses, 0, NULL);
	sprintf(buf, "%s.jr_hits", name);
	stat_reg_counter(sdb, buf,
			"total number of address-predicted hits for JR's",
			&pred->jr_hits, 0, NULL);
	sprintf(buf, "%s.jr_seen", name);
	stat_reg_counter(sdb, buf,
			"total number of JR's seen",
			&pred->jr_seen, 0, NULL);
	sprintf(buf, "%s.jr_non_ras_hits.PP", name);
	stat_reg_counter(sdb, buf,
			"total number of address-predicted hits for non-RAS JR's",
			&pred->jr_non_ras_hits, 0, NULL);
	sprintf(buf, "%s.jr_non_ras_seen.PP", name);
	stat_reg_counter(sdb, buf,
			"total number of non-RAS JR's seen",
			&pred->jr_non_ras_seen, 0, NULL);
	sprintf(buf, "%s.bpred_addr_rate", name);
	sprintf(buf1, "%s.addr_hits / %s.updates", name, name);
	stat_reg_formula(sdb, buf,
			"branch address-prediction rate (i.e., addr-hits/updates)",
			buf1, "%9.4f");
	sprintf(buf, "%s.bpred_dir_rate", name);
	sprintf(buf1, "%s.dir_hits / %s.updates", name, name);
	stat_reg_formula(sdb, buf,
			"branch direction-prediction rate (i.e., all-hits/updates)",
			buf1, "%9.4f");
	sprintf(buf, "%s.bpred_jr_rate", name);
	sprintf(buf1, "%s.jr_hits / %s.jr_seen", name, name);
	stat_reg_formula(sdb, buf,
			"JR address-prediction rate (i.e., JR addr-hits/JRs seen)",
			buf1, "%9.4f");
	sprintf(buf, "%s.bpred_jr_non_ras_rate.PP", name);
	sprintf(buf1, "%s.jr_non_ras_hits.PP / %s.jr_non_ras_seen.PP", name, name);
	stat_reg_formula(sdb, buf,
			"non-RAS JR addr-pred rate (ie, non-RAS JR hits/JRs seen)",
			buf1, "%9.4f");
	sprintf(buf, "%s.retstack_pushes", name);
	stat_reg_counter(sdb, buf,
			"total number of address pushed onto ret-addr stack",
			&pred->retstack_pushes, 0, NULL);
	sprintf(buf, "%s.retstack_pops", name);
	stat_reg_counter(sdb, buf,
			"total number of address popped off of ret-addr stack",
			&pred->retstack_pops, 0, NULL);
	sprintf(buf, "%s.used_ras.PP", name);
	stat_reg_counter(sdb, buf,
			"total number of RAS predictions used",
			&pred->used_ras, 0, NULL);
	sprintf(buf, "%s.ras_hits.PP", name);
	stat_reg_counter(sdb, buf,
			"total number of RAS hits",
			&pred->ras_hits, 0, NULL);
	sprintf(buf, "%s.ras_rate.PP", name);
	sprintf(buf1, "%s.ras_hits.PP / %s.used_ras.PP", name, name);
	stat_reg_formula(sdb, buf,
			"RAS prediction rate (i.e., RAS hits/used RAS)",
			buf1, "%9.4f");
}

	void
bpred_after_priming(struct bpred_t *bpred)
{
	if (bpred == NULL)
		return;

	bpred->lookups = 0;
	bpred->addr_hits = 0;
	bpred->dir_hits = 0;
	bpred->used_ras = 0;
	bpred->used_bimod = 0;
	bpred->used_2lev = 0;
	bpred->jr_hits = 0;
	bpred->jr_seen = 0;
	bpred->misses = 0;
	bpred->retstack_pops = 0;
	bpred->retstack_pushes = 0;
	bpred->ras_hits = 0;
}

#define BIMOD_HASH(PRED, ADDR)						\
	((((ADDR) >> 19) ^ ((ADDR) >> MD_BR_SHIFT)) & ((PRED)->config.bimod.size-1))
/* was: ((baddr >> 16) ^ baddr) & (pred->dirpred.bimod.size-1) */

/* predicts a branch direction */
	char *						/* pointer to counter */
bpred_dir_lookup(struct bpred_dir_t *pred_dir,	/* branch dir predictor inst */
		md_addr_t baddr)		/* branch address */
{
	unsigned char *p = NULL;

	/* Except for jumps, get a pointer to direction-prediction bits */
	switch (pred_dir->class) {
		case BPredTage:
			{		    
				pred_dir->config.tage.altTagComp=NUMBEROFTAGTABLE;
				pred_dir->config.tage.primeTagComp=NUMBEROFTAGTABLE;
				struct tage temp_tage =pred_dir->config.tage;
				for(int i=0;i<NUMBEROFTAGTABLE;i++)
				{
					temp_tage.tag_comp_index[i]=baddr^(baddr>>((int)log2(temp_tage.t1size)))^(temp_tage.folded_history_index[i].folded_history);
				}
				for(int i=0;i<NUMBEROFTAGTABLE;i++)
				{
					temp_tage.tag_comp_index[i] &=((1<<(int)log2(temp_tage.t1size))-1);

				}
				for(int i=0;i<NUMBEROFTAGTABLE;i++)
				{
					temp_tage.tag_comp_tag[i]=baddr^(temp_tage.folded_history_tag[0][i].folded_history)^(temp_tage.folded_history_tag[1][i].folded_history<<1);
					temp_tage.tag_comp_tag[i]&=((1<<temp_tage.tag_size[i])-1);	
				}
				//Base Prediction 
				int base_pred_index =((((baddr) >> 19) ^ ((baddr) >> MD_BR_SHIFT)) & (temp_tage.size-1));
				p=&(pred_dir->config.tage.bimod[base_pred_index].ctr);
				temp_tage.isBimodal=1;
				for(int i=0;i<NUMBEROFTAGTABLE;i++)
				{
					if(temp_tage.tag_comp_entry[i][temp_tage.tag_comp_index[i]].tag==temp_tage.tag_comp_tag[i])
					{
						temp_tage.primeTagComp=i;
						break;
					}
				}
		for(int i=(temp_tage.primeTagComp+1);i<NUMBEROFTAGTABLE;i++)
		{
			if(temp_tage.tag_comp_entry[i][temp_tage.tag_comp_index[i]].tag==temp_tage.tag_comp_tag[i])
			{
				temp_tage.altTagComp=i;
				break;
			}
		}

		if(temp_tage.primeTagComp<NUMBEROFTAGTABLE)
		{	
			struct tag_comp_entry temp_tag_comp_entry;
			temp_tag_comp_entry=temp_tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]];
			if(temp_tage.altTagComp==NUMBEROFTAGTABLE)
			{
				temp_tage.altPred=(*p>>1)&1;
			}
			else
			{	int alt_tag_table_index = temp_tage.altTagComp;
				int tag_index=temp_tage.tag_comp_index[alt_tag_table_index];
				temp_tage.isBimodal=0;
				p=&(pred_dir->config.tage.tag_comp_entry[alt_tag_table_index][tag_index].ctr);
				temp_tage.altPred=(temp_tage.tag_comp_entry[alt_tag_table_index][tag_index].ctr>>((int)ceil(log(TAGCTRMAX))-1))&1;
			}
			if(temp_tag_comp_entry.useful_entry!=0)
			{
				temp_tage.primePred=temp_tag_comp_entry.ctr>>((int)ceil(log(TAGCTRMAX))-1)&1;
				temp_tage.isBimodal=0;	
				p=&(pred_dir->config.tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]].ctr);
			}	
		}
		else
		{       temp_tage.isBimodal=1;
			temp_tage.altPred=(*p>>1)&1;
		}
		pred_dir->config.tage=temp_tage;


	break;
	}
    case BPred2Level:
      {
	int l1index, l2index;

        /* traverse 2-level tables */
        l1index = (baddr >> MD_BR_SHIFT) & (pred_dir->config.two.l1size - 1);
        l2index = pred_dir->config.two.shiftregs[l1index];
        if (pred_dir->config.two.xor)
	  {
#if 1
	    /* this L2 index computation is more "compatible" to McFarling's
	       verison of it, i.e., if the PC xor address component is only
	       part of the index, take the lower order address bits for the
	       other part of the index, rather than the higher order ones */
	    l2index = (((l2index ^ (baddr >> MD_BR_SHIFT))
			& ((1 << pred_dir->config.two.shift_width) - 1))
		       | ((baddr >> MD_BR_SHIFT)
			  << pred_dir->config.two.shift_width));
#else
	    l2index = l2index ^ (baddr >> MD_BR_SHIFT);
#endif
	  }
	else
	  {
	    l2index =
	      l2index
		| ((baddr >> MD_BR_SHIFT) << pred_dir->config.two.shift_width);
	  }
        l2index = l2index & (pred_dir->config.two.l2size - 1);

        /* get a pointer to prediction state information */
        p = &pred_dir->config.two.l2table[l2index];
      }
      break;
    case BPred2bit:
      p = &pred_dir->config.bimod.table[BIMOD_HASH(pred_dir, baddr)];
      break;
    case BPredTaken:
    case BPredNotTaken:
      break;
    default:
      panic("bogus branch direction predictor class");
    }

  return (char *)p;
}

/* probe a predictor for a next fetch address, the predictor is probed
   with branch address BADDR, the branch target is BTARGET (used for
   static predictors), and OP is the instruction opcode (used to simulate
   predecode bits; a pointer to the predictor state entry (or null for jumps)
   is returned in *DIR_UPDATE_PTR (used for updating predictor state),
   and the non-speculative top-of-stack is returned in stack_recover_idx 
   (used for recovering ret-addr stack after mis-predict).  */
md_addr_t				/* predicted branch target addr */
bpred_lookup(struct bpred_t *pred,	/* branch predictor instance */
	     md_addr_t baddr,		/* branch address */
	     md_addr_t btarget,		/* branch target if taken */
	     enum md_opcode op,		/* opcode of instruction */
	     int is_call,		/* non-zero if inst is fn call */
	     int is_return,		/* non-zero if inst is fn return */
	     struct bpred_update_t *dir_update_ptr, /* pred state pointer */
	     int *stack_recover_idx)	/* Non-speculative top-of-stack;
					 * used on mispredict recovery */
{
  struct bpred_btb_ent_t *pbtb = NULL;
  int index, i;
  if (!dir_update_ptr)
    panic("no bpred update record");

  /* if this is not a branch, return not-taken */
  if (!(MD_OP_FLAGS(op) & F_CTRL))
{  
	return 0;
}

  pred->lookups++;

  dir_update_ptr->dir.ras = FALSE;
  dir_update_ptr->pdir1 = NULL;
  dir_update_ptr->pdir2 = NULL;
  dir_update_ptr->pmeta = NULL;
  /* Except for jumps, get a pointer to direction-prediction bits */
  switch (pred->class) {
    case BPredComb:
      if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) != (F_CTRL|F_UNCOND))
	{
	  char *bimod, *twolev, *meta;
	  bimod = bpred_dir_lookup (pred->dirpred.bimod, baddr);
	  twolev = bpred_dir_lookup (pred->dirpred.twolev, baddr);
	  meta = bpred_dir_lookup (pred->dirpred.meta, baddr);
	  dir_update_ptr->pmeta = meta;
	  dir_update_ptr->dir.meta  = (*meta >= 2);
	  dir_update_ptr->dir.bimod = (*bimod >= 2);
	  dir_update_ptr->dir.twolev  = (*twolev >= 2);
	  if (*meta >= 2)
	    {
	      dir_update_ptr->pdir1 = twolev;
	      dir_update_ptr->pdir2 = bimod;
	    }
	  else
	    {
	      dir_update_ptr->pdir1 = bimod;
	      dir_update_ptr->pdir2 = twolev;
	    }
	}
      break;
    case BPredTage:
	if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) != (F_CTRL|F_UNCOND))
	{
	  dir_update_ptr->pdir1 =
	   bpred_dir_lookup (pred->dirpred.tage, baddr);
	}
  	break;	
    case BPred2Level:
      if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) != (F_CTRL|F_UNCOND))
	{
	  dir_update_ptr->pdir1 =
	    bpred_dir_lookup (pred->dirpred.twolev, baddr);
	}
      break;
    case BPred2bit:
      if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) != (F_CTRL|F_UNCOND))
	{
	  dir_update_ptr->pdir1 =
	    bpred_dir_lookup (pred->dirpred.bimod, baddr);
	}
      break;
    case BPredTaken:
      return btarget;
    case BPredNotTaken:
      if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) != (F_CTRL|F_UNCOND))
	{
	  return baddr + sizeof(md_inst_t);
	}
      else
	{
	  return btarget;
	}
    default:
      panic("bogus predictor class");
  }

  /*
   * We have a stateful predictor, and have gotten a pointer into the
   * direction predictor (except for jumps, for which the ptr is null)
   */

  /* record pre-pop TOS; if this branch is executed speculatively
   * and is squashed, we'll restore the TOS and hope the data
   * wasn't corrupted in the meantime. */
  if (pred->retstack.size)
    *stack_recover_idx = pred->retstack.tos;
  else
    *stack_recover_idx = 0;

  /* if this is a return, pop return-address stack */
  if (is_return && pred->retstack.size)
    {
      md_addr_t target = pred->retstack.stack[pred->retstack.tos].target;
      pred->retstack.tos = (pred->retstack.tos + pred->retstack.size - 1)
	                   % pred->retstack.size;
      pred->retstack_pops++;
      dir_update_ptr->dir.ras = TRUE; /* using RAS here */
      return target;
    }

#ifndef RAS_BUG_COMPATIBLE
  /* if function call, push return-address onto return-address stack */
  if (is_call && pred->retstack.size)
    {
      pred->retstack.tos = (pred->retstack.tos + 1)% pred->retstack.size;
      pred->retstack.stack[pred->retstack.tos].target = 
	baddr + sizeof(md_inst_t);
      pred->retstack_pushes++;
    }
#endif /* !RAS_BUG_COMPATIBLE */
  
  /* not a return. Get a pointer into the BTB */
  index = (baddr >> MD_BR_SHIFT) & (pred->btb.sets - 1);

  if (pred->btb.assoc > 1)
    {
      index *= pred->btb.assoc;

      /* Now we know the set; look for a PC match */
      for (i = index; i < (index+pred->btb.assoc) ; i++)
	if (pred->btb.btb_data[i].addr == baddr)
	  {
	    /* match */
	    pbtb = &pred->btb.btb_data[i];
	    break;
	  }
    }	
  else
    {
      pbtb = &pred->btb.btb_data[index];
      if (pbtb->addr != baddr)
	pbtb = NULL;
    }

  /*
   * We now also have a pointer into the BTB for a hit, or NULL otherwise
   */

  /* if this is a jump, ignore predicted direction; we know it's taken. */
  if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) == (F_CTRL|F_UNCOND))
    {
      	return (pbtb ? pbtb->target : 1);
    }

  /* otherwise we have a conditional branch */
  if (pbtb == NULL)
    {
     if(pred->class==BPredTage)
	{	if(!(!!pred->dirpred.tage->config.tage.isBimodal))
		{
			return((*(dir_update_ptr->pdir1)>=((TAGCTRMAX>>1)+1))?1:0);
		}
	} 
	    
	 /* BTB miss -- just return a predicted direction */

        return ((*(dir_update_ptr->pdir1) >= 2)
	      ? /* taken */ 1
	      : /* not taken */ 0);
    }
  else
    {
      	/* BTB hit, so return target if it's a predicted-taken branch */
	if(pred->class==BPredTage)
	{	if(!(!!pred->dirpred.tage->config.tage.isBimodal))
		{
			return((*(dir_update_ptr->pdir1)>=((TAGCTRMAX>>1)+1))?pbtb->target:0);
		}
	}
      return ((*(dir_update_ptr->pdir1) >= 2)
	      ? /* taken */ pbtb->target
	      : /* not taken */ 0);
    }

}

/* Speculative execution can corrupt the ret-addr stack.  So for each
 * lookup we return the top-of-stack (TOS) at that point; a mispredicted
 * branch, as part of its recovery, restores the TOS using this value --
 * hopefully this uncorrupts the stack. */
void
bpred_recover(struct bpred_t *pred,	/* branch predictor instance */
	      md_addr_t baddr,		/* branch address */
	      int stack_recover_idx)	/* Non-speculative top-of-stack;
					 * used on mispredict recovery */
{
  if (pred == NULL)
    return;

  pred->retstack.tos = stack_recover_idx;
}

/* update the branch predictor, only useful for stateful predictors; updates
   entry for instruction type OP at address BADDR.  BTB only gets updated
   for branches which are taken.  Inst was determined to jump to
   address BTARGET and was taken if TAKEN is non-zero.  Predictor 
   statistics are updated with result of prediction, indicated by CORRECT and 
   PRED_TAKEN, predictor state to be updated is indicated by *DIR_UPDATE_PTR 
   (may be NULL for jumps, which shouldn't modify state bits).  Note if
   bpred_update is done speculatively, branch-prediction may get polluted. */
void
bpred_update(struct bpred_t *pred,	/* branch predictor instance */
	     md_addr_t baddr,		/* branch address */
	     md_addr_t btarget,		/* resolved branch target */
	     int taken,			/* non-zero if branch was taken */
	     int pred_taken,		/* non-zero if branch was pred taken */
	     int correct,		/* was earlier addr prediction ok? */
	     enum md_opcode op,		/* opcode of instruction */
	     struct bpred_update_t *dir_update_ptr)/* pred state pointer */
{
  struct bpred_btb_ent_t *pbtb = NULL;
  struct bpred_btb_ent_t *lruhead = NULL, *lruitem = NULL;
  int index, i;
  
  /* don't change bpred state for non-branch instructions or if this
   * is a stateless predictor*/
  if (!(MD_OP_FLAGS(op) & F_CTRL))
    return;

  /* Have a branch here */

  if (correct)
    pred->addr_hits++;

  if (!!pred_taken == !!taken)
    pred->dir_hits++;
  else
    pred->misses++;

  if (dir_update_ptr->dir.ras)
    {
      pred->used_ras++;
      if (correct)
	pred->ras_hits++;
    }
  else if ((MD_OP_FLAGS(op) & (F_CTRL|F_COND)) == (F_CTRL|F_COND))
    {
      if (dir_update_ptr->dir.meta)
	pred->used_2lev++;
      else
	pred->used_bimod++;
    }

  /* keep stats about JR's; also, but don't change any bpred state for JR's
   * which are returns unless there's no retstack */
  if (MD_IS_INDIR(op))
    {
      pred->jr_seen++;
      if (correct)
	pred->jr_hits++;
      
      if (!dir_update_ptr->dir.ras)
	{
	  pred->jr_non_ras_seen++;
	  if (correct)
	    pred->jr_non_ras_hits++;
	}
      else
	{
	  /* return that used the ret-addr stack; no further work to do */
	  return;
	}
    }

  /* Can exit now if this is a stateless predictor */
  if (pred->class == BPredNotTaken || pred->class == BPredTaken)
    return;

  /* 
   * Now we know the branch didn't use the ret-addr stack, and that this
   * is a stateful predictor 
   */

#ifdef RAS_BUG_COMPATIBLE
  /* if function call, push return-address onto return-address stack */
  if (MD_IS_CALL(op) && pred->retstack.size)
    {
      pred->retstack.tos = (pred->retstack.tos + 1)% pred->retstack.size;
      pred->retstack.stack[pred->retstack.tos].target = 
	baddr + sizeof(md_inst_t);
      pred->retstack_pushes++;
    }
#endif /* RAS_BUG_COMPATIBLE */

  /* update L1 table if appropriate */
  /* L1 table is updated unconditionally for combining predictor too */
  if ((MD_OP_FLAGS(op) & (F_CTRL|F_UNCOND)) != (F_CTRL|F_UNCOND) &&
      (pred->class == BPred2Level|| pred->class == BPredComb))
    {
	
	      int l1index, shift_reg;
	      
	      /* also update appropriate L1 history register */
	      l1index =
		(baddr >> MD_BR_SHIFT) & (pred->dirpred.twolev->config.two.l1size - 1);
	      shift_reg =
		(pred->dirpred.twolev->config.two.shiftregs[l1index] << 1) | (!!taken);
	      pred->dirpred.twolev->config.two.shiftregs[l1index] =
		shift_reg & ((1 << pred->dirpred.twolev->config.two.shift_width) - 1);
    }
    

  /* find BTB entry if it's a taken branch (don't allocate for non-taken) */
  if (taken)
    {
      index = (baddr >> MD_BR_SHIFT) & (pred->btb.sets - 1);
      
      if (pred->btb.assoc > 1)
	{
	  index *= pred->btb.assoc;
	  
	  /* Now we know the set; look for a PC match; also identify
	   * MRU and LRU items */
	  for (i = index; i < (index+pred->btb.assoc) ; i++)
	    {
	      if (pred->btb.btb_data[i].addr == baddr)
		{
		  /* match */
		  assert(!pbtb);
		  pbtb = &pred->btb.btb_data[i];
		}
	      
	      dassert(pred->btb.btb_data[i].prev 
		      != pred->btb.btb_data[i].next);
	      if (pred->btb.btb_data[i].prev == NULL)
		{
		  /* this is the head of the lru list, ie current MRU item */
		  dassert(lruhead == NULL);
		  lruhead = &pred->btb.btb_data[i];
		}
	      if (pred->btb.btb_data[i].next == NULL)
		{
		  /* this is the tail of the lru list, ie the LRU item */
		  dassert(lruitem == NULL);
		  lruitem = &pred->btb.btb_data[i];
		}
	    }
	  dassert(lruhead && lruitem);
	  
	  if (!pbtb)
	    /* missed in BTB; choose the LRU item in this set as the victim */
	    pbtb = lruitem;	
	  /* else hit, and pbtb points to matching BTB entry */
	  
	  /* Update LRU state: selected item, whether selected because it
	   * matched or because it was LRU and selected as a victim, becomes 
	   * MRU */
	  if (pbtb != lruhead)
	    {
	      /* this splices out the matched entry... */
	      if (pbtb->prev)
		pbtb->prev->next = pbtb->next;
	      if (pbtb->next)
		pbtb->next->prev = pbtb->prev;
	      /* ...and this puts the matched entry at the head of the list */
	      pbtb->next = lruhead;
	      pbtb->prev = NULL;
	      lruhead->prev = pbtb;
	      dassert(pbtb->prev || pbtb->next);
	      dassert(pbtb->prev != pbtb->next);
	    }
	  /* else pbtb is already MRU item; do nothing */
	}
      else
	pbtb = &pred->btb.btb_data[index];
    }
      
  /* 
   * Now 'p' is a possibly null pointer into the direction prediction table, 
   * and 'pbtb' is a possibly null pointer into the BTB (either to a 
   * matched-on entry or a victim which was LRU in its set)
   */

  /* update state (but not for jumps) */
  if (dir_update_ptr->pdir1)
    {
     //	int choosen_tag_table =1; 
    
	if(pred->class==BPredTage)	
      	{
	      struct tage temp_tage=pred->dirpred.tage->config.tage;
		

		if(temp_tage.primeTagComp < NUMBEROFTAGTABLE)
		{
			if((!!pred_taken)!=(!!temp_tage.altPred))
			{
				/**
				 *If the altPredictor's prediction is not the same as the outcome and the prediction was given by Prime predictor, change the useful_entry bit accordingly.
				 **/
				
					if((!!pred_taken)==(!!taken))
					{
						if (temp_tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]].useful_entry < TAGUSEFULMAX)
							++pred->dirpred.tage->config.tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]].useful_entry;
					}
					else
					{
						if (temp_tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]].useful_entry> 0)
							--pred->dirpred.tage->config.tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]].useful_entry;
					}
			}
			else if((!!taken)!=(temp_tage.altPred))
			{
				if((!!taken)==(!!temp_tage.primePred))
				{
					if (temp_tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]].useful_entry < TAGUSEFULMAX)
							++pred->dirpred.tage->config.tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]].useful_entry;

				}
			}
			
			if(taken)
			{
				if(temp_tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]].ctr <TAGCTRMAX)
				{
					++pred->dirpred.tage->config.tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]].ctr;
				}
			}
			else
			{
				if(temp_tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]].ctr > 0)
				{
					--pred->dirpred.tage->config.tage.tag_comp_entry[temp_tage.primeTagComp][temp_tage.tag_comp_index[temp_tage.primeTagComp]].ctr;
				}
			}
			temp_tage=pred->dirpred.tage->config.tage;
		  }
		 else
		 {
			 if (taken)
			{
			  if (*dir_update_ptr->pdir1 < BASECTRMAX)
				    ++*dir_update_ptr->pdir1;
			}
		      else
			{ /* not taken */
			  if (*dir_update_ptr->pdir1 > 0)
				    --*dir_update_ptr->pdir1;
			}
		 }
		 /**
		  *If the prediction was incorrect, allocate a new entry with a long history table if there is space available in the tag table, else decrement the useful_entry counter of all the table after primary tag predictor table.
		  **/
		 if(((!!pred_taken)!=(!!taken))&&(temp_tage.primeTagComp>0 ))
		 {
			unsigned char isPredEntryCountZero=0;
			for(int i=(temp_tage.primeTagComp-1);i>=0;i--)
			{
				if(temp_tage.tag_comp_entry[i][temp_tage.tag_comp_index[i]].useful_entry==0)
				{
					isPredEntryCountZero=1;
					break;
				}
			}
			if(!isPredEntryCountZero)
			{
				for(int i=(temp_tage.primeTagComp-1);i>=0;i--)
				{
					pred->dirpred.tage->config.tage.tag_comp_entry[i][temp_tage.tag_comp_index[i]].useful_entry--;
				}
			}
			else
			{
				int numberOfZeroUsefulEntry=0;
				int choosen_tag_table=0;
				for(int i=(temp_tage.primeTagComp-1);i>=0;i--)
				{
					if(temp_tage.tag_comp_entry[i][temp_tage.tag_comp_index[i]].useful_entry == 0)

					{
						numberOfZeroUsefulEntry++;
						if(numberOfZeroUsefulEntry == 1)
						{
							choosen_tag_table=i;
						}
						else if(numberOfZeroUsefulEntry >1)
						{
								if(numberOfZeroUsefulEntry == 2)
								{
									choosen_tag_table = i+1;
								}
								else
								{
									choosen_tag_table=i+2;
								}
								}
					}
				}
				if(taken)
				{
					pred->dirpred.tage->config.tage.tag_comp_entry[choosen_tag_table][temp_tage.tag_comp_index[choosen_tag_table]].ctr=((TAGCTRMAX>>1)+1);

				}
				else
				{
					pred->dirpred.tage->config.tage.tag_comp_entry[choosen_tag_table][temp_tage.tag_comp_index[choosen_tag_table]].ctr=(TAGCTRMAX>>1);
				}
				pred->dirpred.tage->config.tage.tag_comp_entry[choosen_tag_table][temp_tage.tag_comp_index[choosen_tag_table]].tag=temp_tage.tag_comp_tag[choosen_tag_table];
				pred->dirpred.tage->config.tage.tag_comp_entry[choosen_tag_table][temp_tage.tag_comp_index[choosen_tag_table]].useful_entry=0;
				
			}
		 }
		 temp_tage=pred->dirpred.tage->config.tage;
		 pred->dirpred.tage->config.tage.clock++;
		if(pred->dirpred.tage->config.tage.clock == (256*1024))
		{
			pred->dirpred.tage->config.tage.clock=0;
			if(temp_tage.clock_flip==1)
			{
					pred->dirpred.tage->config.tage.clock_flip=0;
			}
			else
			{
					pred->dirpred.tage->config.tage.clock_flip=1;
			}
			if(temp_tage.clock_flip==1)
			{
				
				for(int i=0;i<NUMBEROFTAGTABLE;i++)
				{
					for(int j=0;j<temp_tage.t1size;j++)
					{
						pred->dirpred.tage->config.tage.tag_comp_entry[i][j].useful_entry&=1;
					}
				}
			}
			else
			{
				for(int i=0;i<NUMBEROFTAGTABLE;i++)
				{
					for(int j=0;j<temp_tage.t1size;j++)
					{
						pred->dirpred.tage->config.tage.tag_comp_entry[i][j].useful_entry&=2;
					}
				}
			}
		}
		temp_tage=pred->dirpred.tage->config.tage;
		for(int i=temp_tage.geometric_lengths[0];i>=1;i--)
		{
			pred->dirpred.tage->config.tage.geometric_history[i] =temp_tage.geometric_history[i-1];
		}
		if(taken)
		{
			pred->dirpred.tage->config.tage.geometric_history[0]=1;
		}
		else
		{
			pred->dirpred.tage->config.tage.geometric_history[0]=0;
		}
		temp_tage=pred->dirpred.tage->config.tage;
		for(int i=0;i<NUMBEROFTAGTABLE;i++)
		{
			if(temp_tage.geometric_lengths[i]<(((int)log2(temp_tage.t1size))))
			{
				temp_tage.folded_history_index[i].folded_history=0;
				for(int j=(temp_tage.folded_history_tag[0][i].folded_length-1);j>=0;j--)
				{
					temp_tage.folded_history_index[i].folded_history^=temp_tage.geometric_history[j]<<j;
				}
				for(int j=(temp_tage.folded_history_tag[1][i].folded_length-1);j>=0;j--)
				{
					temp_tage.folded_history_index[i].folded_history^=temp_tage.geometric_history[j]<<j;
				}
				temp_tage.folded_history_index[i].folded_history &=((1<<temp_tage.folded_history_tag[0][i].folded_length)-1);
				pred->dirpred.tage->config.tage.folded_history_index[i]=temp_tage.folded_history_index[i];
			}
			else
			{
				pred->dirpred.tage->config.tage.folded_history_index[i].folded_history =(temp_tage.folded_history_index[i].folded_history<<1)^( temp_tage.folded_history_index[i].folded_history>>(temp_tage.folded_history_index[i].folded_length-1))^(temp_tage.geometric_history[0]);
				pred->dirpred.tage->config.tage.folded_history_index[i].folded_history^=temp_tage.geometric_history[temp_tage.geometric_lengths[i]]<<(temp_tage.geometric_lengths[i]%temp_tage.folded_history_index[i].folded_length);
				pred->dirpred.tage->config.tage.folded_history_index[i].folded_history&=((1<<temp_tage.folded_history_index[i].folded_length)-1);
			}

			if(temp_tage.geometric_lengths[i]<temp_tage.tag_size[i])
			{
				temp_tage.folded_history_tag[0][i].folded_history=0;
				temp_tage.folded_history_tag[1][i].folded_history=0;
				for(int j=(temp_tage.folded_history_tag[0][i].folded_length-1);j>=0;j--)
				{
					temp_tage.folded_history_tag[0][i].folded_history^=temp_tage.geometric_history[j]<<j;
				}
				for(int j=(temp_tage.folded_history_tag[1][i].folded_length-1);j>=0;j--)
				{
					temp_tage.folded_history_tag[1][i].folded_history^=temp_tage.geometric_history[j]<<j;
				}
				temp_tage.folded_history_tag[0][i].folded_history &=((1<<temp_tage.folded_history_tag[0][i].folded_length)-1);
				temp_tage.folded_history_tag[1][i].folded_history &=((1<<temp_tage.folded_history_tag[0][i].folded_length)-1);
				pred->dirpred.tage->config.tage.folded_history_tag[0][i]=temp_tage.folded_history_tag[0][i];
				pred->dirpred.tage->config.tage.folded_history_tag[1][i]=temp_tage.folded_history_tag[1][i];

			}
			else
			{
				pred->dirpred.tage->config.tage.folded_history_tag[0][i].folded_history =(temp_tage.folded_history_tag[0][i].folded_history<<1)^( temp_tage.folded_history_tag[0][i].folded_history>>(temp_tage.folded_history_tag[0][i].folded_length-1))^(temp_tage.geometric_history[0]);
				pred->dirpred.tage->config.tage.folded_history_tag[0][i].folded_history^=temp_tage.geometric_history[temp_tage.geometric_lengths[i]]<<(temp_tage.geometric_lengths[i]%temp_tage.folded_history_tag[0][i].folded_length);
				pred->dirpred.tage->config.tage.folded_history_tag[0][i].folded_history&=((1<<temp_tage.folded_history_tag[0][i].folded_length)-1);
				fprintf(stderr,"The folded history of table %d tag1 is %d \n",i,pred->dirpred.tage->config.tage.folded_history_tag[0][i].folded_history);

				pred->dirpred.tage->config.tage.folded_history_tag[1][i].folded_history =(temp_tage.folded_history_tag[1][i].folded_history<<1)^( temp_tage.folded_history_tag[1][i].folded_history>>(temp_tage.folded_history_tag[1][i].folded_length-1))^(temp_tage.geometric_history[0]);
				pred->dirpred.tage->config.tage.folded_history_tag[1][i].folded_history^=temp_tage.geometric_history[temp_tage.geometric_lengths[i]]<<(temp_tage.geometric_lengths[i]%temp_tage.folded_history_tag[1][i].folded_length);
				pred->dirpred.tage->config.tage.folded_history_tag[1][i].folded_history&=((1<<temp_tage.folded_history_tag[1][i].folded_length)-1);
				fprintf(stderr,"The folded history of table %d tag2 is %d \n",i,pred->dirpred.tage->config.tage.folded_history_tag[1][i].folded_history);
			}
		}
      }
      else
      {
	if (taken)
		{
		  if (*dir_update_ptr->pdir1 < 3)
		    ++*dir_update_ptr->pdir1;
		}
	      else
		{ /* not taken */
		  if (*dir_update_ptr->pdir1 > 0)
		    --*dir_update_ptr->pdir1;
		}
      }
    }

  /* combining predictor also updates second predictor and meta predictor */
  /* second direction predictor */
  if (dir_update_ptr->pdir2)
    {
      if (taken)
	{
	  if (*dir_update_ptr->pdir2 < 3)
	    ++*dir_update_ptr->pdir2;
	}
      else
	{ /* not taken */
	  if (*dir_update_ptr->pdir2 > 0)
	    --*dir_update_ptr->pdir2;
	}
    }

  /* meta predictor */
  if (dir_update_ptr->pmeta)
    {
      if (dir_update_ptr->dir.bimod != dir_update_ptr->dir.twolev)
	{
	  /* we only update meta predictor if directions were different */
	  if (dir_update_ptr->dir.twolev == (unsigned int)taken)
	    {
	      /* 2-level predictor was correct */
	      if (*dir_update_ptr->pmeta < 3)
		++*dir_update_ptr->pmeta;
	    }
	  else
	    {
	      /* bimodal predictor was correct */
	      if (*dir_update_ptr->pmeta > 0)
		--*dir_update_ptr->pmeta;
	    }
	}
    }

  /* update BTB (but only for taken branches) */
  if (pbtb)
    {
      /* update current information */
      dassert(taken);

      if (pbtb->addr == baddr)
	{
	  if (!correct)
	    pbtb->target = btarget;
	}
      else
	{
	  /* enter a new branch in the table */
	  pbtb->addr = baddr;
	  pbtb->op = op;
	  pbtb->target = btarget;
	}
    }
//fprintf(stderr,"**********End update function**********\n\n\n");

}
