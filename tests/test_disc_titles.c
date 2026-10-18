#include "check.h"
#include "disc_titles.h"
#include "ps_iso.h"
#include "catalog.h"
#include "timeutil.h"
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>

static void serials(void)
{
	const char *good[] = {
		"BOOT2 = cdrom0:\\SLUS_209.46;1\r\nVER = 1.0\r\n",
		"  boot2\t=\tcdrom0:/DIR/slus_209.46;123  \n",
		"BOOT2=SLUS-20946", "BOOT2 = slus_20946;1\r",
		"BOOT2=SLUS_209.46;1\nBOOT2=SLUS-20946\n",
		"BOOT=cdrom:\\slus_209.46;1\r\n",
		"BOOT=SCUS_949.06;1\nBOOT2=SLUS_209.46;1"
	};
	char s[PS_SERIAL_SIZE];
	CHECK(ps_serial_valid("NGCD-00241")); CHECK(ps_serial_valid("NGCD-0069C"));
	CHECK(!ps_serial_valid("NGCD-0069c")); CHECK(!ps_serial_valid("SLUS-0069C"));
	for (unsigned i=0;i<sizeof(good)/sizeof(*good);i++) {
		CHECK(ps_boot_serial(good[i],strlen(good[i]),s)); CHECK_STR(s,"SLUS-20946");
	}
	const char *bad[] = {"BOOTX = SLUS_209.46;1", "BOOT2X=SLUS_209.46;1",
		"BOOT2=SLUS_20.946;1", "BOOT2=SLUS_209.46;", "BOOT2=SLUS_209.46;A",
		"BOOT2=SLUS_209.46.ELF;1", "BOOT2=homebrew.elf;1", "BOOT2=SLUS_209.A6",
		"BOOT2=SLUS_209.46;1 junk", "BOOT2=SLUS_209.46;1\nBOOT2=SLES_123.45;1"};
	for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) CHECK(!ps_boot_serial(bad[i],strlen(bad[i]),s));
	CHECK(!ps_boot_serial("BOOT2=SLUS_209.46\0",18,s));
}
static void truncation(void)
{
	char b[9];
	disc_title_copy(b,5,"ab\xe6\x97\xa5Z"); CHECK_STR(b,"ab");
	disc_title_copy(b,6,"ab\xe6\x97\xa5Z"); CHECK_STR(b,"ab\xe6\x97\xa5");
	disc_title_copy(b,4,"\xf0\x9f\x8e\xae"); CHECK_STR(b,"");
	disc_title_copy(b,5,"\xf0\x9f\x8e\xae"); CHECK_STR(b,"\xf0\x9f\x8e\xae");
}
#ifdef __linux__
static void wait_work(struct disc_titles *m)
{
	long long end=now_ms()+4000;
	struct psystem *s=m->queue;
	while ((m->queue || m->job.pid) && now_ms()<end) {
		if (disc_titles_step(m) && s) {
			for (int i=0;i<s->ngames;i++)
				if (!strcmp(s->games[i].path,m->completed)) disc_titles_apply(m,s->name,&s->games[i]);
			catalog_sort_games(s,NULL); /* publishing must not skip queued paths after reordering */
		}
		if (m->job.pid) {
			struct pollfd p[2]={{.fd=m->job.fd,.events=POLLIN},{.fd=m->job.done_fd,.events=POLLIN}};
			poll(p,2,100);
		}
	}
	CHECK(!m->queue && !m->job.pid);
}
#endif
static void cache(void)
{
	char root[32]; strcpy(root,fixture_dir());
	char table[128], file[128], helper[128], cachefile[128], other[128];
	snprintf(table,sizeof(table),"%s/titles",root);
	snprintf(file,sizeof(file),"%s/arbitrary ' $(literal).ChD",root);
	snprintf(other,sizeof(other),"%s/other.chd",root);
	snprintf(helper,sizeof(helper),"%s/helper",root);
	snprintf(cachefile,sizeof(cachefile),"%s/cache/identities",root);
	fixture_write(table,"SLUS-20946\tGrand Theft Auto - San Andreas\n");
	fixture_write(file,""); fixture_write(other,"");
	fixture_write(helper,"#!/bin/sh\n[ -f \"$1\" ] || exit 2\nprintf 'SLUS-20946\\n'\n"); chmod(helper,0700);
	struct game games[2]={0};
	str_copy(games[0].path,512,file); str_copy(games[1].path,512,other);
	str_copy(games[0].name,192,"arbitrary"); str_copy(games[1].name,192,"Alpha");
	struct psystem s={.games=games,.ngames=2}; strcpy(s.name,"ps2");
	struct disc_titles m; disc_titles_init(&m,table,table,NULL,NULL,cachefile,helper);
	CHECK(disc_titles_eligible("ps2",file,0)); CHECK(disc_titles_eligible("psx",file,0));
	CHECK(!disc_titles_eligible("ps2",file,1)); CHECK(!disc_titles_eligible("ps2","/x.m3u",0));
	disc_titles_apply(&m,"ps2",games); CHECK_STR(games[0].name,"arbitrary");
#ifdef __linux__
	disc_titles_begin(&m,&s); wait_work(&m); CHECK_INT(m.nentries,2);
	disc_titles_apply(&m,"ps2",games); CHECK_STR(games[0].name,"Grand Theft Auto - San Andreas");
	CHECK_STR(games[0].path,file);
	disc_titles_apply(&m,"ps2",games+1);
	CHECK_INT(catalog_sort_games(&s,file),0);
	CHECK_STR(games[0].path,file); CHECK_STR(games[1].path,other); /* identical names, separate paths */
	strcpy(games[0].name,"Z"); strcpy(games[1].name,"A");
	CHECK_INT(catalog_sort_games(&s,file),1); CHECK_STR(games[1].path,file);
	disc_titles_apply(&m,"ps2",games); disc_titles_apply(&m,"ps2",games+1);
	CHECK_INT(catalog_sort_games(&s,file),0);
	char es[128], xml[1024]; snprintf(es,sizeof(es),"%s/es.cfg",root);
	snprintf(xml,sizeof(xml),"<systemList>\n<system>\n<name>ps2</name>\n<path>%s</path>\n"
	         "<extension>.chd</extension>\n</system>\n</systemList>\n",root);
	fixture_write(es,xml);
	struct catalog catalog;
	CHECK_INT(catalog_load_titles(&catalog,es,"/nonexistent/settings",&m),0);
	CHECK_INT(catalog.n,1);
	if (catalog.n == 1) {
		CHECK_INT(catalog.sys[0].ngames,2);
		CHECK_STR(catalog.sys[0].games[0].name,"Grand Theft Auto - San Andreas");
		CHECK_STR(catalog.sys[0].games[0].path,file);
	}
	catalog_free(&catalog);
	disc_titles_free(&m);
	/* Updating only the database resolves the cached serial without a helper. */
	fixture_write(table,"SLUS-20946\tUpdated title\n");
	fixture_write(helper,"#!/bin/sh\nexit 2\n");
	disc_titles_init(&m,table,table,NULL,NULL,cachefile,helper); CHECK_INT(m.nentries,2);
	disc_titles_apply(&m,"ps2",games); CHECK_STR(games[0].name,"Updated title");
	disc_titles_begin(&m,&s); disc_titles_step(&m);
	CHECK(!m.job.pid && !m.queue); CHECK_INT(m.nentries,2);
	/* Same-size replacement invalidates the inode/ctime signature. */
	unlink(file); fixture_write(file,""); strcpy(games[0].name,"fallback");
	disc_titles_apply(&m,"ps2",games); CHECK_STR(games[0].name,"fallback");
	disc_titles_begin(&m,&s); wait_work(&m); /* transient errors never replace identities */
	disc_titles_free(&m);
	fixture_write(cachefile,"PSDISC\t4\ninvalid\nEND\t0000000000000000\n");
	disc_titles_init(&m,table,table,NULL,NULL,cachefile,helper); CHECK_INT(m.nentries,0);
	/* A stable no-ID is cached, including after reload. */
	fixture_write(helper,"#!/bin/sh\nexit 1\n"); disc_titles_begin(&m,&s); wait_work(&m);
	CHECK_INT(m.nentries,2); disc_titles_free(&m);
	disc_titles_init(&m,table,table,NULL,NULL,cachefile,helper); CHECK_INT(m.nentries,2);
	fixture_write(helper,"#!/bin/sh\nexit 99\n"); disc_titles_begin(&m,&s); disc_titles_step(&m);
	CHECK(!m.job.pid && !m.queue);
	CHECK_INT(m.nentries,2);
	/* Changed while in flight: discard the result, then retry on next open. */
	fixture_write(file,"new");
	fixture_write(helper,"#!/bin/sh\nsleep 0.1\nprintf 'SLUS-20946\\n'\n");
	disc_titles_begin(&m,&s); disc_titles_step(&m); CHECK(m.job.pid>0);
	fixture_write(file,"replacement"); wait_work(&m);
	strcpy(games[0].name,"changed"); disc_titles_apply(&m,"ps2",games); CHECK_STR(games[0].name,"changed");
	/* Cancellation reaps the helper before returning and saves completed entries. */
	disc_titles_begin(&m,&s); disc_titles_step(&m); int pid=m.job.pid;
	disc_titles_stop(&m); CHECK(!m.job.pid); CHECK_INT(waitpid(pid,NULL,WNOHANG),-1);
#endif
	disc_titles_free(&m); fixture_cleanup(root);
}
static void jobs(void)
{
#ifdef __linux__
	struct proc_job j; proc_job_init(&j);
	char *args[]={"/bin/sh","-c","exec sleep 10",NULL};
	CHECK_INT(proc_job_start(&j,args,30),0);
	struct pollfd p={.fd=j.done_fd,.events=POLLIN}; poll(&p,1,60);
	CHECK(proc_job_step(&j)); CHECK_INT(j.status,PROC_TIMEOUT);
	char *early[]={"/bin/sh","-c","exec 1>&-; sleep 0.08; exit 1",NULL};
	CHECK_INT(proc_job_start(&j,early,1000),0);
	p=(struct pollfd){.fd=j.fd,.events=POLLIN}; poll(&p,1,200);
	CHECK(!proc_job_step(&j)); CHECK_INT(j.fd,-1);
	p=(struct pollfd){.fd=j.done_fd,.events=POLLIN}; CHECK_INT(poll(&p,1,1000),1);
	CHECK(proc_job_step(&j)); CHECK_INT(j.status,1);
#endif
}
static void supported_systems(void)
{
#ifdef __linux__
	char root[32]; strcpy(root,fixture_dir());
	char psx_table[128], ps2_table[128], psp_table[128], neocd_table[128], cache[128], helper[128];
	snprintf(psx_table,sizeof(psx_table),"%s/psx.tsv",root);
	snprintf(ps2_table,sizeof(ps2_table),"%s/ps2.tsv",root);
	snprintf(psp_table,sizeof(psp_table),"%s/psp.tsv",root);
	snprintf(neocd_table,sizeof(neocd_table),"%s/neocd.tsv",root);
	snprintf(cache,sizeof(cache),"%s/cache",root);
	snprintf(helper,sizeof(helper),"%s/helper",root);
	fixture_write(psx_table,"SLUS-20946\tPS1 scoped title\n");
	fixture_write(ps2_table,"SLUS-20946\tPS2 scoped title\n");
	fixture_write(psp_table,"SLUS-20946\tPSP scoped title (USA) (En,Fr,De,Es,It,Nl,Pl,Ru) [Disc 1]\n");
	fixture_write(neocd_table,"NGCD-0069C\tNeo Geo CD scoped title\n");
	fixture_write(helper,"#!/bin/sh\ncase \"$1\" in *neocd*|*neogeocd*) printf 'NGCD-0069C\\n';; "
	                     "*) printf 'SLUS-20946\\n';; esac\n"); chmod(helper,0700);
	struct game games[6]={0}; struct psystem systems[6]={0};
	const char *names[]={"psx","ps2","psp","neogeocd","neocd","megadrive"};
	for (int i=0;i<6;i++) {
		snprintf(games[i].path,sizeof(games[i].path),"%s/%s.CHd",root,names[i]);
		fixture_write(games[i].path,""); strcpy(games[i].name,"filename");
		strcpy(systems[i].name,names[i]); systems[i].games=&games[i]; systems[i].ngames=1;
	}
	struct catalog catalog={.sys=systems,.n=6};
	struct disc_titles m; disc_titles_init(&m,ps2_table,psx_table,psp_table,neocd_table,cache,helper);
	disc_titles_begin_catalog(&m,&catalog); wait_work(&m); CHECK_INT(m.nentries,5);
	for (int i=0;i<6;i++) disc_titles_apply(&m,names[i],&games[i]);
	CHECK_STR(games[0].name,"PS1 scoped title"); CHECK_STR(games[1].name,"PS2 scoped title");
	CHECK_STR(games[2].name,"PSP scoped title (USA) [Disc 1]");
	CHECK_STR(games[3].name,"Neo Geo CD scoped title");
	CHECK_STR(games[4].name,"Neo Geo CD scoped title");
	CHECK_STR(games[5].name,"filename");
	CHECK(!disc_titles_lookup(&m,"psx","NGCD-0069C"));
	CHECK(!disc_titles_lookup(&m,"neogeocd","SLUS-20946"));
	CHECK(!disc_titles_eligible("psp",games[2].path,1));
	CHECK(!disc_titles_eligible("psp","/test.iso",0));
	CHECK(!disc_titles_eligible("psp","/test.m3u",0));
	CHECK(disc_titles_lookup(&m,"megadrive","SLUS-20946")==NULL);
	disc_titles_free(&m);
	/* Same cache, only the PS1 title table changes. */
	fixture_write(psx_table,"SLUS-20946\tUpdated PS1 title\n");
	disc_titles_init(&m,ps2_table,psx_table,psp_table,neocd_table,cache,helper);
	disc_titles_apply(&m,"psx",&games[0]); CHECK_STR(games[0].name,"Updated PS1 title");
	disc_titles_begin_catalog(&m,&catalog); disc_titles_step(&m); CHECK(!m.job.pid && !m.queue);
	fixture_write(psp_table,"SLUS-20946\tUpdated PSP title (Europe) (En,Fr)\n");
	disc_titles_free(&m);
	disc_titles_init(&m,ps2_table,psx_table,psp_table,neocd_table,cache,helper);
	disc_titles_apply(&m,"psp",&games[2]); CHECK_STR(games[2].name,"Updated PSP title (Europe)");
	disc_titles_begin_catalog(&m,&catalog); disc_titles_step(&m); CHECK(!m.job.pid && !m.queue);
	/* Neo Geo CD IDs survive reload without a helper, including hexadecimal digits. */
	disc_titles_free(&m);
	fixture_write(neocd_table,"NGCD-0069C\tUpdated Neo Geo CD title\n");
	disc_titles_init(&m,ps2_table,psx_table,psp_table,neocd_table,cache,"/nonexistent/helper");
	for (int i=3;i<5;i++) {
		disc_titles_apply(&m,names[i],&games[i]); CHECK_STR(games[i].name,"Updated Neo Geo CD title");
	}
	disc_titles_free(&m);
	/* Even an otherwise valid version-3 snapshot must be retried by the new reader. */
	FILE *old = fopen(cache,"r+"); CHECK(old != NULL);
	if (old) { CHECK_INT(fseek(old,7,SEEK_SET),0); CHECK_INT(fputc('3',old),'3'); fclose(old); }
	disc_titles_init(&m,ps2_table,psx_table,psp_table,neocd_table,cache,helper);
	CHECK_INT(m.nentries,0);
	disc_titles_free(&m); fixture_cleanup(root);
#endif
}
static void language_labels(void)
{
	char root[32]; strcpy(root,fixture_dir()); char table[128];
	snprintf(table,sizeof(table),"%s/titles",root);
	fixture_write(table,
		"UCUS-00001\tKillzone - Liberation (USA) (En,Fr,De,Es,It,Nl,Pl,Ru)\n"
		"UCUS-00002\tGame (Europe) (En, Fr) (Disc 2) (Ni) [Deluxe Pack]\n"
		"UCUS-00003\tGame (En) (US) (Japan, Asia) (The Beginning)\n"
		"UCUS-00004\tGame (Energy) (En,Demo) (USA, Europe) (En,Fr\n"
		"UCUS-00005\t(En,Fr) Game (Ja,Zh)\n"
		"UCUS-00006\tGame (En,Fr) - Part 2\n");
	struct disc_titles m; disc_titles_init(&m,NULL,NULL,table,NULL,"/nonexistent/cache","/nonexistent/helper");
	CHECK_STR(disc_titles_lookup(&m,"psp","UCUS-00001"),"Killzone - Liberation (USA)");
	CHECK_STR(disc_titles_lookup(&m,"psp","UCUS-00002"),"Game (Europe) (Disc 2) (Ni) [Deluxe Pack]");
	CHECK_STR(disc_titles_lookup(&m,"psp","UCUS-00003"),"Game (US) (Japan, Asia) (The Beginning)");
	CHECK_STR(disc_titles_lookup(&m,"psp","UCUS-00004"),"Game (Energy) (En,Demo) (USA, Europe) (En,Fr");
	CHECK_STR(disc_titles_lookup(&m,"psp","UCUS-00005"),"Game");
	CHECK_STR(disc_titles_lookup(&m,"psp","UCUS-00006"),"Game - Part 2");
	CHECK(!disc_titles_lookup(&m,"psx","UCUS-00001"));
	disc_titles_free(&m); fixture_cleanup(root);
}
static void table_validation(void)
{
	char root[32]; strcpy(root,fixture_dir()); char table[128];
	snprintf(table,sizeof(table),"%s/titles",root);
	const char *bad[] = {"", "SLUS-20946\tTitle", "SLUS-20946\tTitle\ntrailing",
		"SLUS-20946\tTitle\n\n", "SLUS-20946\tTitle\nSLUS-20946\tDuplicate\n"};
	struct disc_titles m;
	for (unsigned i = 0; i < sizeof(bad)/sizeof(*bad); i++) {
		fixture_write(table,bad[i]);
		disc_titles_init(&m,table,NULL,NULL,NULL,"/nonexistent/cache","/nonexistent/helper");
		CHECK_INT(m.ps2.ntitles,0);
		disc_titles_free(&m);
	}
	FILE *f = fopen(table,"w"); CHECK(f != NULL);
	if (f) {
		for (unsigned i = 0; i < 32768; i++) fprintf(f,"AAAA-%05u\tTitle %u\n",i,i);
		fclose(f);
		disc_titles_init(&m,table,NULL,NULL,NULL,"/nonexistent/cache","/nonexistent/helper");
		CHECK_INT(m.ps2.ntitles,32768);
		CHECK_STR(disc_titles_lookup(&m,"ps2","AAAA-32767"),"Title 32767");
		disc_titles_free(&m);
		f = fopen(table,"a"); CHECK(f != NULL);
		if (f) { fputs("AAAA-32768\tOver limit\n",f); fclose(f); }
		disc_titles_init(&m,table,NULL,NULL,NULL,"/nonexistent/cache","/nonexistent/helper");
		CHECK_INT(m.ps2.ntitles,0);
		disc_titles_free(&m);
	}
	fixture_cleanup(root);
}
int main(void)
{
	serials(); truncation(); cache(); jobs(); supported_systems(); language_labels(); table_validation(); return check_report("disc titles");
}
