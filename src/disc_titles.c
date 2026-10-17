#include "disc_titles.h"
#include "catalog.h"
#include "glyph.h"
#include "text.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define ENTRY_MAX 32768U
#define TABLE_MAX (2U*1024U*1024U)
#define BUCKETS 65536U
#define CACHE_HEADER "PSDISC\t3\n" /* cache and resolver format version */
#define HELPER_MS 10000
struct disc_title { char *serial, *title; };
struct disc_identity {
	char path[512], serial[PS_SERIAL_SIZE];
	struct disc_signature signature;
	int status, next;
};
static uint64_t hash_bytes(uint64_t h, const void *data, size_t size)
{
	const unsigned char *p = data;
	while (size--) { h ^= *p++; h *= UINT64_C(1099511628211); }
	return h;
}
static unsigned bucket(const char *path)
{ return (unsigned)hash_bytes(UINT64_C(14695981039346656037), path, strlen(path)) & (BUCKETS-1); }
void disc_title_copy(char *out, size_t size, const char *s)
{
	if (!size) return;
	size_t n = strlen(s);
	if (n >= size) {
		n = size-1;
		while (n && ((unsigned char)s[n] & 0xc0) == 0x80) n--;
	}
	memcpy(out,s,n); out[n] = 0;
}
static int signature(const char *path, struct disc_signature *sig)
{
	struct stat st;
	if (stat(path,&st) || !S_ISREG(st.st_mode)) return 0;
	sig->v[0] = (uint64_t)st.st_size;
#ifdef __APPLE__
	sig->v[1] = (uint64_t)st.st_mtimespec.tv_sec;
	sig->v[2] = (uint64_t)st.st_mtimespec.tv_nsec;
	sig->v[5] = (uint64_t)st.st_ctimespec.tv_sec;
	sig->v[6] = (uint64_t)st.st_ctimespec.tv_nsec;
#else
	sig->v[1] = (uint64_t)st.st_mtim.tv_sec;
	sig->v[2] = (uint64_t)st.st_mtim.tv_nsec;
	sig->v[5] = (uint64_t)st.st_ctim.tv_sec;
	sig->v[6] = (uint64_t)st.st_ctim.tv_nsec;
#endif
	sig->v[3] = (uint64_t)st.st_dev; sig->v[4] = (uint64_t)st.st_ino;
	return 1;
}
static int same(const struct disc_signature *a, const struct disc_signature *b)
{ return !memcmp(a,b,sizeof(*a)); }
static struct disc_identity *identity(const struct disc_titles *m, const char *path)
{
	if (!m->buckets) return NULL;
	for (int i = m->buckets[bucket(path)]; i >= 0; i = m->entries[i].next)
		if (!strcmp(m->entries[i].path,path)) return &m->entries[i];
	return NULL;
}
static int store_identity(struct disc_titles *m, const struct disc_identity *e)
{
	if (!m->buckets) return 0;
	struct disc_identity *old = identity(m,e->path);
	if (old) { int next = old->next; *old = *e; old->next = next; return 1; }
	if (m->nentries >= ENTRY_MAX) return 0;
	if (m->nentries == m->capacity) {
		size_t cap = m->capacity ? m->capacity*2 : 128;
		void *p = realloc(m->entries,cap*sizeof(*e));
		if (!p) return 0;
		m->entries = p; m->capacity = cap;
	}
	unsigned b = bucket(e->path);
	m->entries[m->nentries] = *e;
	m->entries[m->nentries].next = m->buckets[b];
	m->buckets[b] = (int)m->nentries++;
	return 1;
}
static const char *language_end(const char *p)
{
	/* Only language-code groups disappear; regions, disc numbers and subtitles stay. */
	static const char *const languages[] = {
		"En","Fr","De","Es","It","Nl","Pt","Ja","Zh","Ko","Pl","Ru",
		"Sv","No","Da","Fi","El","Cs","Sk","Hu","Ro","Bg","Hr","Sr",
		"Sl","Et","Lv","Lt","Uk","Tr","Ar","He","Hi","Th","Vi","Id",
		"Ms","Is","Ca","Eu","Gl","Af","Ga","Mk","Sq","Bn","Fa","Sw",
		"Ta","Te","Ur","Bs","Be","Cy","Mt","Tl","Uz","Ka","Hy"
	};
	p++;
	for (unsigned count = 0; count < 64; count++) {
		while (*p == ' ') p++;
		if (!p[0] || !p[1]) return NULL;
		int found = 0;
		for (unsigned i = 0; i < sizeof(languages)/sizeof(*languages); i++)
			if (p[0] == languages[i][0] && p[1] == languages[i][1]) { found = 1; break; }
		if (!found) return NULL;
		p += 2;
		while (*p == ' ') p++;
		if (*p == ')') return p+1;
		if (*p++ != ',') return NULL;
	}
	return NULL;
}
static void strip_languages(char *title)
{
	char *out = title;
	for (const char *p = title; *p;) {
		const char *end = *p == '(' ? language_end(p) : NULL;
		if (end) {
			while (out > title && out[-1] == ' ') out--;
			p = end;
			while (*p == ' ') p++;
			if (out > title && *p) *out++ = ' ';
		} else *out++ = *p++;
	}
	*out = 0;
}
static void load_table(struct disc_title_table *m, const char *path)
{
	if (!path) return;
	FILE *f = fopen(path,"rb");
	if (!f) return;
	if (fseek(f,0,SEEK_END)) { fclose(f); return; }
	long size = ftell(f);
	if (size <= 0 || size > TABLE_MAX || fseek(f,0,SEEK_SET)) { fclose(f); return; }
	char *data = malloc((size_t)size+1);
	if (!data) { fclose(f); return; }
	int valid = fread(data,1,(size_t)size,f) == (size_t)size && !memchr(data,0,(size_t)size);
	fclose(f); data[size] = 0;
	/* Allocate for this table, rather than reserving ENTRY_MAX for each system. */
	size_t nrows = 0;
	for (long i = 0; valid && i < size; i++) if (data[i] == '\n') nrows++;
	if (!nrows || nrows > ENTRY_MAX) { free(data); return; }
	struct disc_title *rows = malloc(nrows*sizeof(*rows));
	if (!rows) { free(data); return; }
	size_t count = 0;
	for (char *p = data; valid && *p;) {
		char *end = strchr(p,'\n'), *tab = strchr(p,'\t');
		if (!end || !tab || tab >= end || count == nrows) { valid = 0; break; }
		*end = 0; *tab = 0;
		if (!ps_serial_valid(p) || !tab[1] || (count && strcmp(rows[count-1].serial,p) >= 0)) {
			valid = 0; break;
		}
		for (char *q = tab+1; q < end; q++) if ((unsigned char)*q < 32 || (unsigned char)*q == 127) valid = 0;
		strip_languages(tab+1);
		if (!tab[1]) { valid = 0; break; }
		rows[count++] = (struct disc_title){p,tab+1}; p = end+1;
	}
	if (!valid) { free(data); free(rows); return; }
	m->table = data; m->titles = rows; m->ntitles = count;
}
static const struct disc_title_table *table_for(const struct disc_titles *m, const char *system)
{
	if (!strcmp(system,"ps2")) return &m->ps2;
	if (!strcmp(system,"psx")) return &m->psx;
	if (!strcmp(system,"psp")) return &m->psp;
	return NULL;
}
int disc_titles_supported(const char *system)
{
	return !strcmp(system,"ps2") || !strcmp(system,"psx") || !strcmp(system,"psp");
}
const char *disc_titles_lookup(const struct disc_titles *db, const char *system, const char *serial)
{
	const struct disc_title_table *m = table_for(db,system);
	if (!m) return NULL;
	size_t lo = 0, hi = m->ntitles;
	while (lo < hi) {
		size_t mid = lo+(hi-lo)/2;
		int c = strcmp(serial,m->titles[mid].serial);
		if (!c) return m->titles[mid].title;
		if (c < 0) hi = mid; else lo = mid+1;
	}
	return NULL;
}
int disc_titles_eligible(const char *sys, const char *path, int folder)
{
	const char *dot = strrchr(path,'.');
	return !folder && disc_titles_supported(sys) &&
	       path[0] == '/' && dot && !strcasecmp(dot,".chd");
}
void disc_titles_apply(const struct disc_titles *m, const char *sys, struct game *g)
{
	if (!disc_titles_eligible(sys,g->path,g->folder)) return;
	struct disc_identity *e = identity(m,g->path);
	struct disc_signature sig;
	if (!e || !signature(g->path,&sig) || !same(&sig,&e->signature)) return;
	const char *title = e->status ? NULL : disc_titles_lookup(m,sys,e->serial);
	if (title) disc_title_copy(g->name,sizeof(g->name),title);
	else {
		const char *slash = strrchr(g->path,'/');
		disc_title_copy(g->name,sizeof(g->name),slash ? slash+1 : g->path);
		char *dot = strrchr(g->name,'.');
		if (dot) *dot = 0;
	}
	text_compose(g->name);
}
static int digit(char c)
{
	if (c >= '0' && c <= '9') return c-'0';
	if (c >= 'a' && c <= 'f') return c-'a'+10;
	return -1;
}
static int parse_entry(char *line, struct disc_identity *e)
{
	memset(e,0,sizeof(*e));
	char *save, *s = strtok_r(line,"\t\n",&save);
	for (int i = 0; i < 7; i++) {
		if (!s || !*s || strspn(s,"0123456789") != strlen(s)) return 0;
		errno = 0; char *end;
		e->signature.v[i] = strtoull(s,&end,10);
		if (errno || *end) return 0;
		s = strtok_r(NULL,"\t\n",&save);
	}
	if (e->signature.v[2] >= 1000000000 || e->signature.v[6] >= 1000000000 || !s ||
	    (strcmp(s,"0") && strcmp(s,"1"))) return 0;
	e->status = *s-'0'; s = strtok_r(NULL,"\t\n",&save);
	if (!s || (e->status ? strcmp(s,"-") != 0 : !ps_serial_valid(s))) return 0;
	if (!e->status) memcpy(e->serial,s,PS_SERIAL_SIZE);
	s = strtok_r(NULL,"\t\n",&save);
	if (!s) return 0;
	size_t n = strlen(s);
	if (!n || n%2 || n/2 >= sizeof(e->path)) return 0;
	for (size_t i = 0; i < n; i += 2) {
		int a = digit(s[i]), b = digit(s[i+1]);
		if (a < 0 || b < 0 || !(a*16+b)) return 0;
		e->path[i/2] = (char)(a*16+b);
	}
	return e->path[0] == '/' && !strtok_r(NULL,"\t\n",&save);
}
static void clear_cache(struct disc_titles *m)
{
	m->nentries = 0;
	if (m->buckets) for (unsigned i = 0; i < BUCKETS; i++) m->buckets[i] = -1;
}
static void load_cache(struct disc_titles *m)
{
	FILE *f = fopen(m->cache,"rb");
	if (!f) return;
	struct stat st;
	if (fstat(fileno(f),&st) || st.st_size > 48*1024*1024) { fclose(f); return; }
	char line[1536];
	int valid = fgets(line,sizeof(line),f) && !strcmp(line,CACHE_HEADER), ended = 0;
	uint64_t h = UINT64_C(14695981039346656037);
	while (valid && fgets(line,sizeof(line),f)) {
		if (!strncmp(line,"END\t",4)) {
			char expected[32]; snprintf(expected,sizeof(expected),"END\t%016" PRIx64 "\n",h);
			ended = !strcmp(line,expected) && fgetc(f) == EOF;
			break;
		}
		if (!strchr(line,'\n')) { valid = 0; break; }
		h = hash_bytes(h,line,strlen(line));
		struct disc_identity e;
		if (!parse_entry(line,&e) || identity(m,e.path) || !store_identity(m,&e)) valid = 0;
	}
	if (!valid || !ended || ferror(f)) clear_cache(m);
	fclose(f);
}
void disc_titles_init(struct disc_titles *m, const char *ps2_table, const char *psx_table,
                      const char *psp_table, const char *cache, const char *helper)
{
	memset(m,0,sizeof(*m)); proc_job_init(&m->job);
	str_copy(m->cache,sizeof(m->cache),cache); str_copy(m->helper,sizeof(m->helper),helper);
	m->buckets = malloc(BUCKETS*sizeof(*m->buckets));
	clear_cache(m); load_table(&m->ps2,ps2_table); load_table(&m->psx,psx_table);
	load_table(&m->psp,psp_table);
	if (m->buckets) load_cache(m);
}
int disc_titles_save(struct disc_titles *m)
{
	if (!m->dirty) return 0;
	char dir[512], tmp[544]; str_copy(dir,sizeof(dir),m->cache);
	char *slash = strrchr(dir,'/');
	if (!slash) return -1;
	*slash = 0;
	for (char *p = dir+1; *p; p++) if (*p == '/') { *p=0; mkdir(dir,0700); *p='/'; }
	mkdir(dir,0700);
	snprintf(tmp,sizeof(tmp),"%s.XXXXXX",m->cache);
	int fd = mkstemp(tmp);
	if (fd < 0) return -1;
	FILE *f = fdopen(fd,"w");
	if (!f) { close(fd); unlink(tmp); return -1; }
	int ok = fputs(CACHE_HEADER,f) >= 0;
	uint64_t h = UINT64_C(14695981039346656037);
	for (size_t i = 0; ok && i < m->nentries; i++) {
		const struct disc_identity *e = &m->entries[i];
		char line[1536]; size_t n = 0;
		for (int k = 0; k < 7; k++) n += (size_t)snprintf(line+n,sizeof(line)-n,"%" PRIu64 "\t",e->signature.v[k]);
		n += (size_t)snprintf(line+n,sizeof(line)-n,"%d\t%s\t",e->status,e->status ? "-" : e->serial);
		static const char hex[] = "0123456789abcdef";
		for (const unsigned char *p = (const unsigned char *)e->path; *p; p++) {
			line[n++] = hex[*p>>4]; line[n++] = hex[*p&15];
		}
		line[n++] = '\n'; h = hash_bytes(h,line,n);
		ok = fwrite(line,1,n,f) == n;
	}
	if (fprintf(f,"END\t%016" PRIx64 "\n",h) < 0 || fflush(f) || fsync(fd)) ok = 0;
	if (fclose(f)) ok = 0;
	if (ok && !rename(tmp,m->cache)) {
		int d = open(dir,O_RDONLY | O_DIRECTORY);
		if (d >= 0) { fsync(d); close(d); }
		m->dirty = 0; return 0;
	}
	unlink(tmp); return -1;
}
void disc_titles_stop(struct disc_titles *m)
{
	proc_job_cancel(&m->job); m->queue = NULL; m->pending[0] = 0;
	free(m->paths); m->paths = NULL; m->npaths = 0;
	disc_titles_save(m);
}
static void begin_systems(struct disc_titles *m, struct psystem *systems, int count)
{
	disc_titles_stop(m); m->cursor = 0;
	if (!systems || !m->buckets || access(m->helper,X_OK)) return;
	int n = 0;
	for (int k = 0; k < count; k++) {
		const struct disc_title_table *t = table_for(m,systems[k].name);
		if (!t || !t->ntitles) continue;
		for (int i = 0; i < systems[k].ngames && n < (int)ENTRY_MAX; i++)
			if (disc_titles_eligible(systems[k].name,systems[k].games[i].path,systems[k].games[i].folder)) n++;
	}
	if (!n) return;
	m->paths = calloc((size_t)n, sizeof(*m->paths));
	if (!m->paths) return;
	for (int k = 0; k < count; k++) {
		struct psystem *s = &systems[k];
		const struct disc_title_table *t = table_for(m,s->name);
		if (!t || !t->ntitles) continue;
		for (int i = 0; i < s->ngames && m->npaths < n; i++)
			if (disc_titles_eligible(s->name,s->games[i].path,s->games[i].folder))
				str_copy(m->paths[m->npaths++],512,s->games[i].path);
	}
	m->queue = systems;
}
void disc_titles_begin(struct disc_titles *m, struct psystem *s)
{
	begin_systems(m,s,1);
}
void disc_titles_begin_catalog(struct disc_titles *m, struct catalog *catalog)
{
	begin_systems(m,catalog->sys,catalog->n);
}
int disc_titles_step(struct disc_titles *m)
{
	int changed = 0;
	if (m->job.pid) {
		if (!proc_job_step(&m->job)) return 0;
		struct disc_signature now;
		int status = m->job.status;
		char serial[PS_SERIAL_SIZE] = "";
		int valid = status == 1 && m->job.length == 0;
		if (!status && !m->job.overflow && m->job.length == PS_SERIAL_SIZE &&
		    m->job.output[PS_SERIAL_SIZE-1] == '\n') {
			memcpy(serial,m->job.output,PS_SERIAL_SIZE-1);
			valid = ps_serial_valid(serial);
		}
		if (valid && signature(m->pending,&now) && same(&now,&m->signature)) {
			struct disc_identity e = {0};
			str_copy(e.path,sizeof(e.path),m->pending); memcpy(e.serial,serial,sizeof(serial));
			e.signature = now; e.status = status;
			if (store_identity(m,&e)) {
				m->dirty++; changed = 1; str_copy(m->completed,sizeof(m->completed),e.path);
				if (m->dirty >= 8) disc_titles_save(m);
			}
		}
		m->pending[0] = 0;
	}
	/* Check only a bounded number per UI iteration. Already cached entries do no disc reads. */
	for (int count = 0; m->queue && count < 64; count++) {
		if (m->cursor >= m->npaths) {
			m->queue = NULL; free(m->paths); m->paths = NULL; m->npaths = 0;
			disc_titles_save(m); break;
		}
		const char *path = m->paths[m->cursor++];
		if (!signature(path,&m->signature)) continue;
		struct disc_identity *e = identity(m,path);
		if (e && same(&m->signature,&e->signature)) continue;
		str_copy(m->pending,sizeof(m->pending),path);
		char *argv[] = {m->helper,m->pending,NULL};
		if (proc_job_start(&m->job,argv,HELPER_MS) < 0) {
			/* Operational failure: retry on the next list open, with no persistent negative. */
			m->queue = NULL; disc_titles_save(m);
		}
		break;
	}
	return changed;
}
void disc_titles_free(struct disc_titles *m)
{
	disc_titles_stop(m); free(m->ps2.table); free(m->ps2.titles);
	free(m->psx.table); free(m->psx.titles); free(m->entries); free(m->buckets);
	free(m->psp.table); free(m->psp.titles);
	memset(m,0,sizeof(*m)); proc_job_init(&m->job);
}
