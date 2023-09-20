/* udp-flood - 2011-2023 Willy Tarreau <w@1wt.eu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_THREADS 64
#define MAX_SENDERS 1000

struct errmsg {
	char *msg;
	int size;
	int len;
};

const int zero = 0;
const int one = 1;

/* roughly 5 kB of random text */
const char *lorem =
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Aliquam "
	"tempus fringilla ullamcorper. Vestibulum ante ipsum primis in "
	"faucibus orci luctus et ultrices posuere cubilia curae; Aliquam "
	"quis risus dictum, eleifend dolor a, venenatis sem. Vestibulum "
	"ante ipsum primis in faucibus orci luctus et ultrices posuere "
	"cubilia curae; Nulla molestie hendrerit varius. Cras quis mauris "
	"at nunc sodales egestas. Vivamus bibendum nec quam et "
	"bibendum. Morbi quis arcu porttitor, eleifend nisi blandit, "
	"aliquam neque. Cras mollis eu felis quis tincidunt. Nam non nibh "
	"ipsum. Sed consequat nulla eget tortor dapibus iaculis. Vivamus "
	"aliquet, turpis sit amet eleifend cursus, velit nibh elementum "
	"urna, suscipit faucibus lectus justo et lacus. Nulla at lectus "
	"volutpat, facilisis metus vel, porta est. Aliquam in eros a urna "
	"suscipit dignissim at eu leo. Nunc congue sem at urna egestas "
	"tincidunt. Nullam at pulvinar velit, a dictum est. Aliquam id "
	"sollicitudin purus. Ut aliquam maximus viverra. Pellentesque "
	"tristique risus eros, malesuada viverra lorem bibendum at. Aliquam "
	"mattis lacus quis massa maximus malesuada. Proin ut sem nec est "
	"dignissim dignissim. Donec sagittis erat lobortis arcu pretium "
	"porttitor. Donec eu tellus sodales felis rutrum rhoncus. Aliquam "
	"malesuada neque ligula, interdum imperdiet enim varius tincidunt. "
	"Nulla condimentum euismod tortor quis sollicitudin. Duis massa "
	"arcu, sagittis vitae velit et, rutrum consectetur risus. "
	"Suspendisse bibendum gravida fermentum. Proin rutrum at dui at "
	"commodo. Nulla vitae laoreet arcu. Proin imperdiet tortor eget "
	"massa lacinia auctor. Pellentesque quis congue lacus. Fusce id "
	"augue quam. Duis eget erat sed turpis scelerisque porttitor ac "
	"lobortis sem. Duis at mauris vitae urna volutpat fringilla. Etiam "
	"viverra nibh nisl, at cursus orci mollis vitae. Aliquam bibendum "
	"tortor eu nunc vestibulum, finibus vehicula elit porttitor. "
	"Aliquam erat volutpat. Vestibulum a ex vel magna finibus gravida "
	"in at tellus. Phasellus a tincidunt est. Suspendisse potenti. "
	"Quisque mauris mi, mattis ut libero vitae, scelerisque condimentum "
	"lorem. Duis bibendum neque id ex eleifend luctus. Aliquam erat "
	"volutpat. Orci varius natoque penatibus et magnis dis parturient "
	"montes, nascetur ridiculus mus. Integer enim turpis, auctor vel "
	"venenatis sollicitudin, scelerisque id purus. Proin elementum "
	"consequat rhoncus. Aenean posuere felis eget placerat lacinia. "
	"Nam laoreet luctus velit vitae ultrices. Mauris porta feugiat "
	"ante, ut tincidunt velit pretium vitae. Sed convallis ante nec "
	"egestas eleifend. Sed imperdiet justo ut auctor bibendum. Donec "
	"ligula sem, finibus nec tellus at, gravida lobortis tellus. "
	"Curabitur posuere magna nisl. Mauris dignissim volutpat "
	"ullamcorper. Nulla gravida et nisl sed vestibulum. Duis libero "
	"metus, pellentesque nec porttitor vel, dictum at enim. Mauris "
	"luctus risus eget ligula porta tempus. Quisque eu egestas sem. "
	"Pellentesque ac arcu molestie, vulputate velit ut, pellentesque "
	"dui. Nam id consequat quam. Aenean sit amet tellus a enim "
	"imperdiet sodales. In vestibulum ut massa eget tempus. Curabitur "
	"eu luctus elit, quis cursus odio. Nullam porta, urna a dapibus "
	"elementum, purus enim porttitor enim, a ultrices enim ex id sem. "
	"Maecenas varius ullamcorper felis, sit amet ullamcorper libero "
	"volutpat a. Suspendisse accumsan lacinia dolor, sit amet lobortis "
	"urna mattis ac. Duis vitae aliquam lorem, a porttitor odio. "
	"Vestibulum eget magna vitae leo feugiat accumsan a et nisi. Nam "
	"pretium nec orci ac volutpat. Aliquam sodales, justo at ultricies "
	"porta, felis dolor placerat purus, sit amet faucibus leo nisi et "
	"erat. Sed faucibus tincidunt libero, ac venenatis ante malesuada "
	"eget. Orci varius natoque penatibus et magnis dis parturient "
	"montes, nascetur ridiculus mus. Sed mi purus, lobortis sit amet "
	"leo at, venenatis elementum tortor. In quis ultrices dui. Cras "
	"nec elementum nulla. Aliquam imperdiet, mauris et elementum "
	"hendrerit, nibh ante commodo nisl, vel iaculis est purus malesuada "
	"sem. Quisque et nulla metus. Ut ullamcorper placerat nisi. Ut "
	"rhoncus mollis fringilla. Etiam urna tellus, ullamcorper id "
	"lobortis eget, finibus id nisl. Sed id nunc rutrum, ullamcorper "
	"odio in, mattis eros. Sed et condimentum eros, at molestie est. "
	"Ut eu nulla sit amet lorem molestie sagittis. Nullam cursus massa "
	"a nulla dictum, ac fermentum nibh ornare. Donec ut efficitur "
	"libero. Nunc odio ipsum, facilisis et erat et, viverra placerat "
	"neque. Proin id dignissim justo, in efficitur tortor. Curabitur "
	"eget nisl lectus. Morbi eu arcu ut nulla accumsan tincidunt. "
	"Mauris malesuada sed metus vitae pulvinar. Aliquam maximus elit "
	"nec nunc malesuada, eu porttitor urna vestibulum. Nullam "
	"placerat, eros in vestibulum maximus, mauris ligula dapibus orci, "
	"eu tincidunt urna tortor eget enim. Aliquam aliquet tortor quis "
	"arcu tincidunt, nec fringilla lectus accumsan. Aenean semper, "
	"purus malesuada sagittis euismod, velit eros varius nunc, et "
	"vehicula velit risus et elit. Donec ut fringilla neque, vitae "
	"lacinia dui. Morbi aliquam lectus mauris, eget placerat leo "
	"mollis vel. In purus ligula, tristique quis posuere sodales, "
	"ultricies eu nulla. Vivamus et lorem sed mi pulvinar varius sit "
	"amet ac dolor. Vestibulum id mollis nibh, ut consectetur turpis. "
	"Donec interdum mattis lectus tincidunt interdum. Pellentesque "
	"aliquam maximus tellus, quis laoreet ante imperdiet et. Lorem "
	"ipsum dolor sit justo.\n";

/* used to create a date */
const char *monthname[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* frequency counter: counts events per second */
struct freq_ctr {
	unsigned int curr_sec;
	unsigned int curr_ctr;
	unsigned int prev_ctr;
};

/* fields used by syslog, all must contain the trailing space */
int log_prio = 134; // LOG_LOCAL0 + LOG_INFO
const char *log_host = "localhost";
const char *log_tag  = "loggen:";

static unsigned int cfg_threads;
static unsigned int cfg_bitrate;
static unsigned int cfg_pktrate;
static unsigned int cfg_rampup;
static unsigned int cfg_minsize;
static unsigned int cfg_maxsize;
static unsigned int cfg_verbose;
static unsigned long long cfg_duration; /* microseconds */
static unsigned int cfg_count;
static unsigned int cfg_senders;
static struct sockaddr_storage cfg_addr;
static int cfg_addrlen;
static char *address = "";
unsigned int statistical_prng_state = 0x12345678;

/* startup time */
static struct timeval start_time;

/* describes one sender */
struct sender {
	int fd;                 /* socket to use when sending */
	unsigned int counter;   /* per-sender packet counter */
	time_t last_update;     /* last tv_sec we rebuilt the header */
	int hdr_len;            /* header length */
	char hdr[256];          /* per-sender message header */
} __attribute__((aligned(64)));

struct thread_data {
	pthread_t pth;
	struct timeval now;     /* the thread's local time */
	/* measured pkt rate / bit rate */
	struct freq_ctr meas_pktrate; // pkt / s
	struct freq_ctr meas_bitrate; // kbit / s
	unsigned int cfg_pktrate;     // pkt/s or zero
	unsigned int cfg_bitrate;     // kbit/s or zero
	unsigned int cfg_count;       // total packets to send on this thread
	unsigned int cfg_senders;     // senders on this thread
	unsigned int first_sender;    // first sender on this thread
	unsigned int toterr;
	unsigned int totok;
	unsigned int tot_wait;
} __attribute__((aligned(64)));

static struct sender senders[MAX_SENDERS];
static struct thread_data thread_data[MAX_THREADS];

/* Multiply the two 32-bit operands and shift the 64-bit result right 32 bits.
 * This is used to compute fixed ratios by setting one of the operands to
 * (2^32*ratio).
 */
static inline unsigned int mul32hi(unsigned int a, unsigned int b)
{
	return ((unsigned long long)a * b + a) >> 32;
}

/* Xorshift RNGs from http://www.jstatsoft.org/v08/i14/paper.
 * This has a (2^32)-1 period, only zero is never returned.
 */
static inline unsigned int statistical_prng()
{
	unsigned int x = statistical_prng_state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return statistical_prng_state = x;
}

/* returns a random number between 0 and <range> - 1 that is evenly distributed
 * over the range.
 */
static inline uint statistical_prng_range(uint range)
{
	return mul32hi(statistical_prng(), range ? range - 1 : 0);
}

char *utoa(unsigned int n, char *buffer, int size)
{
	char *pos;

	pos = buffer + size - 1;
	*pos-- = '\0';

	do {
		*pos-- = '0' + n % 10;
		n /= 10;
	} while (n && pos >= buffer);
	return pos + 1;
}

/* converts str in the form [<ipv4>|<ipv6>|<hostname>]:port to struct sockaddr_storage.
 * Returns < 0 with err set in case of error.
 */
int addr_to_ss(char *str, struct sockaddr_storage *ss, struct errmsg *err)
{
	char *range;

	/* look for the addr/port delimiter, it's the last colon. */
	if ((range = strrchr(str, ':')) == NULL) {
		err->len = snprintf(err->msg, err->size, "Missing port number: '%s'\n", str);
		return -1;
	}

	*range++ = 0;

	memset(ss, 0, sizeof(*ss));

	if (strrchr(str, ':') != NULL) {
		/* IPv6 address contains ':' */
		ss->ss_family = AF_INET6;
		((struct sockaddr_in6 *)ss)->sin6_port = htons(atoi(range));

		if (!inet_pton(ss->ss_family, str, &((struct sockaddr_in6 *)ss)->sin6_addr)) {
			err->len = snprintf(err->msg, err->size, "Invalid server address: '%s'\n", str);
			return -1;
		}
	}
	else {
		ss->ss_family = AF_INET;
		((struct sockaddr_in *)ss)->sin_port = htons(atoi(range));

		if (*str == '*' || *str == '\0') { /* INADDR_ANY */
			((struct sockaddr_in *)ss)->sin_addr.s_addr = INADDR_ANY;
			return 0;
		}

		if (!inet_pton(ss->ss_family, str, &((struct sockaddr_in *)ss)->sin_addr)) {
			struct hostent *he = gethostbyname(str);

			if (he == NULL) {
				err->len = snprintf(err->msg, err->size, "Invalid server name: '%s'\n", str);
				return -1;
			}
			((struct sockaddr_in *)ss)->sin_addr = *(struct in_addr *) *(he->h_addr_list);
		}
	}

	return 0;
}

/*
 * returns the difference, in microseconds, between tv1 and tv2, which must
 * be ordered.
 */
static inline long long tv_diff(struct timeval *tv1, struct timeval *tv2)
{
	long long ret;

	ret = (long long)(tv2->tv_sec - tv1->tv_sec) * 1000000LL;
	ret += tv2->tv_usec - tv1->tv_usec;
	return ret;
}

/* Rotate a frequency counter when current period is over. Must not be called
 * during a valid period. It is important that it correctly initializes a null
 * area.
 */
static inline void rotate_freq_ctr(struct timeval *now, struct freq_ctr *ctr)
{
	ctr->prev_ctr = ctr->curr_ctr;
	if (now->tv_sec - ctr->curr_sec != 1) {
		/* we missed more than one second */
		ctr->prev_ctr = 0;
	}
	ctr->curr_sec = now->tv_sec;
	ctr->curr_ctr = 0; /* leave it at the end to help gcc optimize it away */
}

/* Update a frequency counter by <inc> incremental units. It is automatically
 * rotated if the period is over. It is important that it correctly initializes
 * a null area.
 */
static inline void update_freq_ctr(struct timeval *now, struct freq_ctr *ctr, unsigned int inc)
{
	if (ctr->curr_sec == now->tv_sec) {
		ctr->curr_ctr += inc;
		return;
	}
	rotate_freq_ctr(now, ctr);
	ctr->curr_ctr = inc;
	/* Note: later we may want to propagate the update to other counters */
}

/* returns the number of remaining events that can occur on this freq counter
 * while respecting <freq> and taking into account that <pend> events are
 * already known to be pending. Returns 0 if limit was reached.
 */
unsigned int freq_ctr_remain(struct timeval *now, struct freq_ctr *ctr, unsigned int freq, unsigned int pend)
{
	unsigned int frac_prev_sec;
	unsigned int curr, past;
	unsigned int age;

	curr = 0;
	age = now->tv_sec - ctr->curr_sec;

	if (age <= 1) {
		past = ctr->curr_ctr;
		if (!age) {
			curr = past;
			past = ctr->prev_ctr;
		}
		/* fraction of previous second left */
		frac_prev_sec = (1000000U - now->tv_usec) * 4294U;
		curr += mul32hi(past, frac_prev_sec);
	}
	curr += pend;

	if (curr >= freq)
		return 0;
	return freq - curr;
}

/* return the expected wait time in us before the next event may occur,
 * respecting frequency <freq>, and assuming there may already be some pending
 * events. It returns zero if we can proceed immediately, otherwise the wait
 * time, which will be rounded down 1us for better accuracy, with a minimum
 * of one us.
 */
unsigned int next_event_delay(struct timeval *now, struct freq_ctr *ctr, unsigned int freq, unsigned int pend)
{
	unsigned int frac_prev_sec;
	unsigned int curr, past;
	unsigned int wait, age;

	past = 0;
	curr = 0;
	age = now->tv_sec - ctr->curr_sec;

	if (age <= 1) {
		past = ctr->curr_ctr;
		if (!age) {
			curr = past;
			past = ctr->prev_ctr;
		}
		/* fraction of previous second left */
		frac_prev_sec = (1000000U - now->tv_usec) * 4294U;
		curr += mul32hi(past, frac_prev_sec);
	}
	curr += pend;

	if (curr < freq)
		return 0;

	/* Enough events, let's wait. Each event takes 1/freq sec, thus
	 * 1000000/freq us.
	 */
	curr -= freq;
	wait = curr * 1000000ULL / (freq ? freq : 1);
	return wait > 1 ? wait : 1;
}

/* wait this delay from <now> and update <now> with the most recent date known */
void wait_micro(struct timeval *now, unsigned long long delay)
{
	struct timeval end;
	unsigned int remain;

	end.tv_sec  = now->tv_sec + delay / 1000000;
	end.tv_usec = now->tv_usec + delay % 1000000;
	while (end.tv_usec >= 1000000) {
		end.tv_usec -= 1000000;
		end.tv_sec++;
	}

	while (1) {
		gettimeofday(now, NULL);
		if (now->tv_sec > end.tv_sec)
			break;
		if (now->tv_sec == end.tv_sec && now->tv_usec >= end.tv_usec)
			break;
		remain = tv_diff(now, &end);
		if (remain >= 10000)
			usleep(9000);
		/* otherwise do active wait */
	}
}

#define ADD_IOV(IOV, CNT, PTR, LEN) ({			\
	(IOV)[CNT].iov_base = (PTR);			\
	(IOV)[CNT].iov_len  = (LEN);			\
	(CNT)++;					\
	(IOV)[(CNT)-1].iov_len;  /* return size */	\
})

void *flood(void *arg)
{
	unsigned long long pkt;
	struct iovec iovec[3]; // hdr, counter, msg
	struct msghdr msghdr;
	char counter_buf[30];
	char *counter_ptr;
	int counter_len;
	long long diff = 0;
	unsigned int x;
	int lorem_len = strlen(lorem);
	const char *lorem_end = lorem + lorem_len;
	unsigned rampup = cfg_rampup;
	time_t prev_sec = 0;
	int len = 0;
	int base_len, extra_len;
	int thr_num = (long)arg;
	struct thread_data *thr = &thread_data[thr_num];
	int sender = thr->first_sender;
	int stop_sender = sender + thr->cfg_senders;

	if (!cfg_maxsize)
		cfg_maxsize = 1024;

	if (cfg_maxsize > lorem_len)
		cfg_maxsize = lorem_len;

	msghdr.msg_iov     = iovec;
	msghdr.msg_iovlen  = 0;
	msghdr.msg_name    = NULL; // use connected address
	msghdr.msg_namelen = 0;
	msghdr.msg_control = NULL;
	msghdr.msg_controllen = 0;
	msghdr.msg_flags = 0;

	gettimeofday(&thr->now, NULL);

	for (pkt = 0; pkt < thr->cfg_count; pkt++) {
		if (pkt && (thr->cfg_pktrate || thr->cfg_bitrate)) {
			unsigned int wait_us1, wait_us2, wait_us;
			unsigned int eff_pktrate = thr->cfg_pktrate;
			unsigned int eff_bitrate = thr->cfg_bitrate;

			gettimeofday(&thr->now, NULL);
			if (rampup) {
				diff = tv_diff(&start_time, &thr->now);
				if (diff < rampup) {
					/* startup in t^4 */
					unsigned int throttle;

					throttle = (unsigned long long)~0U * diff / rampup;  // 0 to 2^31-1
					throttle = ((unsigned long long)throttle * throttle) >> 32;
					throttle = ((unsigned long long)throttle * throttle) >> 32;

					eff_pktrate = ((unsigned long long)eff_pktrate * throttle) >> 32;
					if (!eff_pktrate)
						eff_pktrate = 1;

					eff_bitrate = ((unsigned long long)eff_bitrate * throttle) >> 32;
					if (!eff_bitrate)
						eff_bitrate = 10; // allow to send at least an avg packet
				}
				else
					rampup = 0; // finished ramping up
			}

			wait_us1 = eff_pktrate ? next_event_delay(&thr->now, &thr->meas_pktrate, eff_pktrate, 0) : 0;
			wait_us2 = eff_bitrate ? next_event_delay(&thr->now, &thr->meas_bitrate, eff_bitrate, 0) : 0;

			wait_us = !eff_bitrate ? wait_us1 : !eff_pktrate ? wait_us2 :
				(wait_us1 > wait_us2) ? wait_us1 : wait_us2;

			if (wait_us) {
				struct timeval old_now = thr->now;
				wait_micro(&thr->now, wait_us);
				diff = tv_diff(&old_now, &thr->now);
				__atomic_add_fetch(&thr->tot_wait, diff, __ATOMIC_RELAXED);
			}
		}
		else if ((pkt & 63) == 0) {
			gettimeofday(&thr->now, NULL); // get time once in a while at least
		}

		/* maybe it's time to stop ? */
		if (cfg_duration) {
			diff = tv_diff(&start_time, &thr->now);
			if (diff >= cfg_duration)
				break;
		}

		if (thr->now.tv_sec != senders[sender].last_update) {
			/* time changed, rebuild the header */
			struct tm tm;

			senders[sender].last_update = thr->now.tv_sec;
			localtime_r(&thr->now.tv_sec, &tm);

			if (cfg_senders == 1 || !*log_host) {
				senders[sender].hdr_len =
					snprintf(senders[sender].hdr, sizeof(senders[sender].hdr),
						 "<%d> %s %2d %02d:%02d:%02d %s%s%s ",
						 log_prio, monthname[tm.tm_mon], tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
						 *log_host ? log_host : "", *log_host ? " " : "", log_tag);
			} else {
				senders[sender].hdr_len =
					snprintf(senders[sender].hdr, sizeof(senders[sender].hdr),
						 "<%d> %s %2d %02d:%02d:%02d %s-%d %s ",
						 log_prio, monthname[tm.tm_mon], tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
						 log_host, sender, log_tag);
			}
		}

		if (thr->now.tv_sec != prev_sec) {
			prev_sec = thr->now.tv_sec;
			if (cfg_verbose && thr_num == 0) {
				unsigned int tot_wait = 0;
				unsigned int totpkt = 0;
				unsigned int toterr = 0;
				unsigned int totok = 0;
				unsigned int v, t;

				for (t = 0; t < cfg_threads; t++) {
					totok  += __atomic_load_n(&thread_data[t].totok, __ATOMIC_RELAXED);
					toterr += __atomic_load_n(&thread_data[t].toterr, __ATOMIC_RELAXED);

					/* load and reset tot_wait */
					v = __atomic_load_n(&thread_data[t].tot_wait, __ATOMIC_RELAXED);
					tot_wait += v;
					if (v)
						__atomic_sub_fetch(&thread_data[t].tot_wait, v, __ATOMIC_RELAXED);
				}
				totpkt = totok + toterr;

				printf("idle %5.2f%%  sent %u/%u (%.2f%%)  err %u (%.2f%%)\n",
				       tot_wait / (cfg_threads * 10000.0),
				       totpkt, cfg_count, totpkt * 100.0 / cfg_count,
				       toterr, toterr * 100.0 / (totpkt ? totpkt : 1));
			}
		}

		msghdr.msg_iovlen = 0;
		len = ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, senders[sender].hdr, senders[sender].hdr_len);

		/* write the counter in ASCII and replace the trailing zero with a space */
		counter_ptr = utoa(senders[sender].counter++, counter_buf, sizeof(counter_buf));
		counter_buf[sizeof(counter_buf) - 1] = ' ';
		counter_len = counter_buf + sizeof(counter_buf) - counter_ptr;
		len += ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, counter_ptr, counter_len);

		/* append random-sized text, terminated with LF. Let's first
		 * produce a cubic random from 0 to 4286583807 (~2^32) centered
		 * around 536870912 (2^29), then we scale it down to the text
		 * length. This means that for a 5.3kB text, the avg message
		 * size will be around 660 bytes long. We enforce a minimum of
		 * cfg_minsize and a maximum of cfg_maxsize. Note that the latter
		 * might be increased by one to send the LF which is always sent.
		 */
		base_len = cfg_minsize > len ? cfg_minsize - len : 0;
		extra_len = cfg_maxsize > base_len + len + 2 ? cfg_maxsize - base_len - len - 2 : 2;

		x = statistical_prng();
		x = (x >> 22) * ((x >> 11) & 2047) * (x & 2047);
		x = mul32hi(x, extra_len - 2) + base_len;

		len += ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, (char *)lorem_end - x, x);

		if (sendmsg(senders[sender].fd, &msghdr, MSG_NOSIGNAL | MSG_DONTWAIT) < 0)
			__atomic_add_fetch(&thr->toterr, 1, __ATOMIC_RELAXED);
		else
			__atomic_add_fetch(&thr->totok, 1, __ATOMIC_RELAXED);

		if (thr->cfg_pktrate)
			update_freq_ctr(&thr->now, &thr->meas_pktrate, 1);

		if (thr->cfg_bitrate) {
			/* We'll count the IP traffic (len+28). We're counting
			 * in kbps (125 bytes). Since we're sending random-sized
			 * datagrams that are often shorter than 1kbit we also
			 * add 1kbit/2 (62 bytes) to compensate for the loss of
			 * accuracy.
			 */
			update_freq_ctr(&thr->now, &thr->meas_bitrate, (len + 28 + 62) / 125);
		}

		sender++;
		if (sender >= stop_sender)
			sender = thr->first_sender;
	}

	diff = tv_diff(&start_time, &thr->now);
	printf("%llu packets sent in %lld us\n", pkt, diff);
	pthread_exit(NULL);
}

void die(int err, const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(err);
}

void die_err(int err, const char *msg)
{
	perror(msg);
	exit(err);
}

int main(int argc, char **argv)
{
	struct errmsg err;
	char hostname[256];
	char *prog = *argv;
	unsigned int t;
	int sender;

	setlinebuf(stdout);

	if (gethostname(hostname, sizeof(hostname) - 1) == 0)
		log_host = hostname;

	err.len = 0;
	err.size = 100;
	err.msg = malloc(err.size);

	--argc; ++argv;

	while (argc && **argv == '-') {
		if (argc > 1 && strcmp(*argv, "-u") == 0) {
			address = *++argv;
			argc--;
		}
		else if (argc > 1 && strcmp(*argv, "-r") == 0) {
			cfg_pktrate = atol(*++argv);
			argc--;
		}
		else if (argc > 1 && strcmp(*argv, "-t") == 0) {
			cfg_threads = atoi(*++argv);
			if (cfg_threads > MAX_THREADS)
				die(1, "Too many threads\n");
			argc--;
		}
		else if (argc > 1 && strcmp(*argv, "-b") == 0) {
			cfg_bitrate = atol(*++argv);
			argc--;
		}
		else if (argc > 1 && strcmp(*argv, "-d") == 0) {
			cfg_duration = atol(*++argv) * 1000000ULL;
			if (!cfg_count)
				cfg_count = ~0U;
			argc--;
		}
		else if (argc > 1 && strcmp(*argv, "-m") == 0) {
			cfg_minsize = atol(*++argv);
			argc--;
		}
		else if (argc > 1 && strcmp(*argv, "-M") == 0) {
			cfg_maxsize = atol(*++argv);
			argc--;
		}
		else if (argc > 1 && strcmp(*argv, "-n") == 0) {
			cfg_count = atol(*++argv);
			argc--;
		}
		else if (argc > 1 && strcmp(*argv, "-s") == 0) {
			cfg_rampup = atol(*++argv) * 1000U;
			argc--;
		}
		else if (argc > 1 && strcmp(*argv, "-S") == 0) {
			cfg_senders = atoi(*++argv);
			if (cfg_senders > MAX_SENDERS)
				die(1, "Too many senders\n");
			argc--;
		}
		else if (strcmp(*argv, "-v") == 0) {
			cfg_verbose = 1;
		}
		else if (argc > 1 && strcmp(*argv, "-h") == 0) {
			strncpy(hostname, *++argv, sizeof(hostname) - 1);
			hostname[sizeof(hostname)-1] = 0;
			log_host = hostname;
			argc--;
		}
		else
			break;
		argc--;
		argv++;
	}

	if (argc > 0 || !*address) {
		fprintf(stderr,
			"Usage: %s [options]\n"
			"  -u <addr:port> : where to send the UDP logs (ipv4:port)\n"
			"  -t <threads>   : use this number of threads to send (def: 1)\n"
			"  -h <hostname>  : host name to advertise. Empty value supported. (def: $hostname)\n"
			"  -n <count>     : send no more than this number of packets (def: 1)\n"
			"  -r <pktrate>   : limit output pkt rate to this number of messages per second\n"
			"  -b <kbps>      : limit output bandwidth to this number of kbps\n"
			"  -s <time>      : slowly ramp up the -r/-b values over this number of milliseconds\n"
			"  -d <duration>  : automatically stop the test after this time in seconds\n"
			"  -m <size>      : minimum message size (def: 0)\n"
			"  -M <size>      : maximum message size (def: 1024, max ~5300)\n"
			"  -S <senders>   : use this number of independent senders (sockets and counters)\n"
			"  -v             : enable verbose mode\n"
			"\n", prog);
		exit(1);
	}

	if (addr_to_ss(address, &cfg_addr, &err) < 0) {
		fprintf(stderr, "%s\n", err.msg);
		exit(1);
	}

	cfg_addrlen = (cfg_addr.ss_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);

	cfg_threads = cfg_threads ? cfg_threads : 1;
	cfg_senders = cfg_senders ? cfg_senders : 1;
	cfg_count   = cfg_count ? cfg_count : 1;

	/* distribute rates and counters for each thread */
	if (cfg_count < cfg_threads || cfg_senders < cfg_threads ||
	    (cfg_pktrate && cfg_pktrate < cfg_threads) ||
	    (cfg_bitrate && cfg_bitrate < cfg_threads))
		die(1, "Please lower the number of threads: count, senders, pktrate and bitrate cannot be lower than the number of threads");

	for (sender = 0; sender < cfg_senders; sender++) {
		if ((senders[sender].fd = socket(cfg_addr.ss_family, SOCK_DGRAM, 0)) == -1)
			die_err(1, "socket");

		if (setsockopt(senders[sender].fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1)
			die_err(1, "setsockopt(SO_REUSEADDR)");

		if (connect(senders[sender].fd, (struct sockaddr *)&cfg_addr, cfg_addrlen) == -1)
			die_err(1, "connect()");
	}

	for (t = 0; t < cfg_threads; t++) {
		thread_data[t].cfg_count = cfg_count / (cfg_threads - t);
		cfg_count -= thread_data[t].cfg_count;

		thread_data[t].cfg_pktrate = cfg_pktrate / (cfg_threads - t);
		cfg_pktrate -= thread_data[t].cfg_pktrate;

		thread_data[t].cfg_bitrate = cfg_bitrate / (cfg_threads - t);
		cfg_bitrate -= thread_data[t].cfg_bitrate;

		if (t > 0)
			thread_data[t].first_sender = thread_data[t-1].first_sender + thread_data[t-1].cfg_senders;
		thread_data[t].cfg_senders = cfg_senders / (cfg_threads - t);
		cfg_senders -= thread_data[t].cfg_senders;
	}

	gettimeofday(&start_time, NULL);


	for (t = 0; t < cfg_threads; t++)
		pthread_create(&thread_data[t].pth, NULL, flood, (void *)(long)t);

	for (t = 0; t < cfg_threads; t++)
		pthread_join(thread_data[t].pth, NULL);

	return 0;
}
