// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include	<u.h>
#include	<libc.h>
#include	"gg.h"
#include	"opt.h"
#include	"../../pkg/runtime/funcdata.h"

static void allocauto(Prog* p);
static int pointermap(Sym*, int, Node*);
static void gcsymbol(Sym*, Node*);

void
compile(Node *fn)
{
	Plist *pl;
	Node nod1, *n, *gcnod;
	Prog *ptxt, *p, *p1;
	int32 lno;
	Type *t;
	Iter save;
	vlong oldstksize;
	NodeList *l;
	Sym *gcsym;
	static int ngcsym;

	if(newproc == N) {
		newproc = sysfunc("newproc");
		deferproc = sysfunc("deferproc");
		deferreturn = sysfunc("deferreturn");
		panicindex = sysfunc("panicindex");
		panicslice = sysfunc("panicslice");
		throwreturn = sysfunc("throwreturn");
	}

	lno = setlineno(fn);

	if(fn->nbody == nil) {
		if(pure_go || memcmp(fn->nname->sym->name, "init·", 6) == 0)
			yyerror("missing function body", fn);
		goto ret;
	}

	saveerrors();

	// set up domain for labels
	clearlabels();

	curfn = fn;
	dowidth(curfn->type);

	if(curfn->type->outnamed) {
		// add clearing of the output parameters
		t = structfirst(&save, getoutarg(curfn->type));
		while(t != T) {
			if(t->nname != N) {
				n = nod(OAS, t->nname, N);
				typecheck(&n, Etop);
				curfn->nbody = concat(list1(n), curfn->nbody);
			}
			t = structnext(&save);
		}
	}
	
	order(curfn);
	if(nerrors != 0)
		goto ret;
	
	hasdefer = 0;
	walk(curfn);
	if(nerrors != 0)
		goto ret;
	if(flag_race)
		racewalk(curfn);
	if(nerrors != 0)
		goto ret;

	continpc = P;
	breakpc = P;

	pl = newplist();
	pl->name = curfn->nname;

	setlineno(curfn);

	nodconst(&nod1, types[TINT32], 0);
	ptxt = gins(ATEXT, isblank(curfn->nname) ? N : curfn->nname, &nod1);
	if(fn->dupok)
		ptxt->TEXTFLAG = DUPOK;
	afunclit(&ptxt->from, curfn->nname);

	ginit();

	snprint(namebuf, sizeof namebuf, "gc·%d", ngcsym++);
	gcsym = lookup(namebuf);
	gcnod = newname(gcsym);
	gcnod->class = PEXTERN;

	nodconst(&nod1, types[TINT32], FUNCDATA_GC);
	gins(AFUNCDATA, &nod1, gcnod);

	for(t=curfn->paramfld; t; t=t->down)
		gtrack(tracksym(t->type));

	for(l=fn->dcl; l; l=l->next) {
		n = l->n;
		if(n->op != ONAME) // might be OTYPE or OLITERAL
			continue;
		switch(n->class) {
		case PAUTO:
		case PPARAM:
		case PPARAMOUT:
			nodconst(&nod1, types[TUINTPTR], l->n->type->width);
			p = gins(ATYPE, l->n, &nod1);
			p->from.gotype = ngotype(l->n);
			break;
		}
	}

	genlist(curfn->enter);

	retpc = nil;
	if(hasdefer || curfn->exit) {
		p1 = gjmp(nil);
		retpc = gjmp(nil);
		patch(p1, pc);
	}

	genlist(curfn->nbody);
	gclean();
	checklabels();
	if(nerrors != 0)
		goto ret;
	if(curfn->endlineno)
		lineno = curfn->endlineno;

	if(curfn->type->outtuple != 0)
		ginscall(throwreturn, 0);

	if(retpc)
		patch(retpc, pc);
	ginit();
	if(hasdefer)
		ginscall(deferreturn, 0);
	if(curfn->exit)
		genlist(curfn->exit);
	gclean();
	if(nerrors != 0)
		goto ret;

	pc->as = ARET;	// overwrite AEND
	pc->lineno = lineno;

	if(!debug['N'] || debug['R'] || debug['P']) {
		regopt(ptxt);
	}

	oldstksize = stksize;
	allocauto(ptxt);
	
	// Emit garbage collection symbol.
	gcsymbol(gcsym, fn);

	if(0)
		print("allocauto: %lld to %lld\n", oldstksize, (vlong)stksize);

	setlineno(curfn);
	if((int64)stksize+maxarg > (1ULL<<31))
		yyerror("stack frame too large (>2GB)");

	defframe(ptxt);

	if(0)
		frame(0);

ret:
	lineno = lno;
}

static void
gcsymbol(Sym *gcsym, Node *fn)
{
	int off;

	off = 0;
	off = duint32(gcsym, off, stksize); // size of local block
	off = pointermap(gcsym, off, fn); // pointer bitmap for args (must be last)
	ggloblsym(gcsym, off, 0, 1);
}

static void
walktype1(Type *t, vlong *xoffset, Bvec *bv)
{
	vlong fieldoffset, i, o;
	Type *t1;

	if(t->align > 0 && (*xoffset % t->align) != 0)
	 	fatal("walktype1: invalid initial alignment, %T", t);

	switch(t->etype) {
	case TINT8:
	case TUINT8:
	case TINT16:
	case TUINT16:
	case TINT32:
	case TUINT32:
	case TINT64:
	case TUINT64:
	case TINT:
	case TUINT:
	case TUINTPTR:
	case TBOOL:
	case TFLOAT32:
	case TFLOAT64:
	case TCOMPLEX64:
	case TCOMPLEX128:
		*xoffset += t->width;
		break;

	case TPTR32:
	case TPTR64:
	case TUNSAFEPTR:
	case TFUNC:
	case TCHAN:
	case TMAP:
		if(*xoffset % widthptr != 0)
			fatal("walktype1: invalid alignment, %T", t);
		bvset(bv, *xoffset / widthptr);
		*xoffset += t->width;
		break;

	case TSTRING:
		// struct { byte *str; intgo len; }
		if(*xoffset % widthptr != 0)
			fatal("walktype1: invalid alignment, %T", t);
		bvset(bv, *xoffset / widthptr);
		*xoffset += t->width;
		break;

	case TINTER:
		// struct { Itab* tab;  union { void* ptr, uintptr val } data; }
		// or, when isnilinter(t)==true:
		// struct { Type* type; union { void* ptr, uintptr val } data; }
		if(*xoffset % widthptr != 0)
			fatal("walktype1: invalid alignment, %T", t);
		bvset(bv, *xoffset / widthptr);
		bvset(bv, (*xoffset + widthptr) / widthptr);
		*xoffset += t->width;
		break;

	case TARRAY:
		// The value of t->bound is -1 for slices types and >0 for
		// for fixed array types.  All other values are invalid.
		if(t->bound < -1)
			fatal("walktype1: invalid bound, %T", t);
		if(isslice(t)) {
			// struct { byte* array; uintgo len; uintgo cap; }
			if(*xoffset % widthptr != 0)
				fatal("walktype1: invalid TARRAY alignment, %T", t);
			bvset(bv, *xoffset / widthptr);
			*xoffset += t->width;
		} else if(!haspointers(t->type))
				*xoffset += t->width;
		else
			for(i = 0; i < t->bound; ++i)
				walktype1(t->type, xoffset, bv);
		break;

	case TSTRUCT:
		o = 0;
		for(t1 = t->type; t1 != T; t1 = t1->down) {
			fieldoffset = t1->width;
			*xoffset += fieldoffset - o;
			walktype1(t1->type, xoffset, bv);
			o = fieldoffset + t1->type->width;
		}
		*xoffset += t->width - o;
		break;

	default:
		fatal("walktype1: unexpected type, %T", t);
	}
}

static void
walktype(Type *type, Bvec *bv)
{
	vlong xoffset;

	// Start the walk at offset 0.  The correct offset will be
	// filled in by the first type encountered during the walk.
	xoffset = 0;
	walktype1(type, &xoffset, bv);
}

// Compute a bit vector to describes the pointer containing locations
// in the argument list.
static int
pointermap(Sym *gcsym, int off, Node *fn)
{
	Type *thistype, *inargtype, *outargtype;
	Bvec *bv;
	int32 i;

	thistype = getthisx(fn->type);
	inargtype = getinargx(fn->type);
	outargtype = getoutargx(fn->type);
	bv = bvalloc(fn->type->argwid / widthptr);
	if(thistype != nil)
		walktype(thistype, bv);
	if(inargtype != nil)
		walktype(inargtype, bv);
	if(outargtype != nil)
		walktype(outargtype, bv);
	off = duint32(gcsym, off, bv->n);
	for(i = 0; i < bv->n; i += 32)
		off = duint32(gcsym, off, bv->b[i/32]);
	free(bv);
	return off;
}

// Sort the list of stack variables.  autos after anything else,
// within autos, unused after used, and within used on reverse alignment.
// non-autos sort on offset.
static int
cmpstackvar(Node *a, Node *b)
{
	if (a->class != b->class)
		return (a->class == PAUTO) ? 1 : -1;
	if (a->class != PAUTO) {
		if (a->xoffset < b->xoffset)
			return -1;
		if (a->xoffset > b->xoffset)
			return 1;
		return 0;
	}
	if ((a->used == 0) != (b->used == 0))
		return b->used - a->used;
	return b->type->align - a->type->align;

}

// TODO(lvd) find out where the PAUTO/OLITERAL nodes come from.
static void
allocauto(Prog* ptxt)
{
	NodeList *ll;
	Node* n;
	vlong w;

	if(curfn->dcl == nil)
		return;

	// Mark the PAUTO's unused.
	for(ll=curfn->dcl; ll != nil; ll=ll->next)
		if (ll->n->class == PAUTO)
			ll->n->used = 0;

	markautoused(ptxt);

	listsort(&curfn->dcl, cmpstackvar);

	// Unused autos are at the end, chop 'em off.
	ll = curfn->dcl;
	n = ll->n;
	if (n->class == PAUTO && n->op == ONAME && !n->used) {
		// No locals used at all
		curfn->dcl = nil;
		stksize = 0;
		fixautoused(ptxt);
		return;
	}

	for(ll = curfn->dcl; ll->next != nil; ll=ll->next) {
		n = ll->next->n;
		if (n->class == PAUTO && n->op == ONAME && !n->used) {
			ll->next = nil;
			curfn->dcl->end = ll;
			break;
		}
	}

	// Reassign stack offsets of the locals that are still there.
	stksize = 0;
	for(ll = curfn->dcl; ll != nil; ll=ll->next) {
		n = ll->n;
		if (n->class != PAUTO || n->op != ONAME)
			continue;

		dowidth(n->type);
		w = n->type->width;
		if(w >= MAXWIDTH || w < 0)
			fatal("bad width");
		stksize += w;
		stksize = rnd(stksize, n->type->align);
		if(thechar == '5')
			stksize = rnd(stksize, widthptr);
		if(stksize >= (1ULL<<31)) {
			setlineno(curfn);
			yyerror("stack frame too large (>2GB)");
		}
		n->stkdelta = -stksize - n->xoffset;
	}

	fixautoused(ptxt);

	// The debug information needs accurate offsets on the symbols.
	for(ll = curfn->dcl ;ll != nil; ll=ll->next) {
		if (ll->n->class != PAUTO || ll->n->op != ONAME)
			continue;
		ll->n->xoffset += ll->n->stkdelta;
		ll->n->stkdelta = 0;
	}
}
