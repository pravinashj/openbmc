#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <systemd/sd-bus.h>
#define DBUS_NAME "org.openbmc.HostIpmikcs"
#define INTF_NAME "org.openbmc.HostIpmi"
#define OBJ_NAME "/org/openbmc/HostIpmi/1"
#define TOTAL_FD 4
#define ENABLE_KCS0 "echo 1 > /sys/devices/platform/ahb/ahb\:apb/1e789000.kcs0/enable" 
#define ENABLE_KCS1 "echo 1 > /sys/devices/platform/ahb/ahb\:apb/1e789000.kcs1/enable" 
#define ENABLE_KCS2 "echo 1 > /sys/devices/platform/ahb/ahb\:apb/1e789000.kcs2/enable" 
#define ENABLE_KCS3 "echo 1 > /sys/devices/platform/ahb/ahb\:apb/1e789000.kcs3/enable" 
#define REQ_BUFF_SIZE 256	
#define RES_BUFF_SIZE 300	

struct ipmi_msg {
	uint8_t netfn;
	uint8_t lun;
	uint8_t seq;
	uint8_t cmd;
	uint8_t cc; /* Only used on responses */
	uint8_t *data;
	size_t data_len;
};

struct kcs_queue {
	struct ipmi_msg req;
	struct ipmi_msg rsp;
	int expired;
	sd_bus_message *call;
	struct kcs_queue *next;
};

struct fd_queue
{
	int fd;
	struct fd_queue *next;
};

struct fd_queue *hptr=NULL;
int kcs_fd[TOTAL_FD],response[TOTAL_FD]={0};
struct sd_bus *bus;
pthread_mutex_t lock;
char res_buf[RES_BUFF_SIZE];


void enq(int fd)
{
	pthread_mutex_lock(&lock);
	struct fd_queue *temp=malloc(sizeof(struct fd_queue));
	static struct fd_queue *last;
	temp->fd=fd;
	temp->next=NULL;
	if(hptr==NULL)
		hptr=temp;
	else
		last->next=temp;
	last=temp;
	pthread_mutex_unlock(&lock);
}

int get_fd(void)
{
	int i;
	static struct fd_queue *temp;
	i=hptr->fd;
	temp=hptr;
	if(hptr->next==NULL)
		hptr=NULL;
	else
		hptr=hptr->next;
	free(temp);
	return i;
}
//KCS channels enabling
void kcs_dev_enable(void) 
{
	system(ENABLE_KCS0); 
	system(ENABLE_KCS1); 
	system(ENABLE_KCS2); 
	system(ENABLE_KCS3); 
}

int get_cur_fd(int fd)
{
	int i;
	for(i=0;i<TOTAL_FD;i++)
		if(fd==kcs_fd[i])
			break;

	return i;

}



int method_send_message_kcs(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{

	struct kcs_queue *kcs_msg;
	uint8_t *data; 
	size_t data_sz;
	uint8_t netfn, lun, seq, cmd, cc;
	int fd,cur_fd;
	/*
	 * Doesn't say it anywhere explicitly but it looks like returning 0 or
	 * negative is BAD...
	 */
	int r = 1;
	kcs_msg=malloc(sizeof(struct kcs_queue));
	r = sd_bus_message_read(msg, "yyyyy", &seq, &netfn, &lun, &cmd, &cc);
	if (r < 0) {
		printf("Couldn't parse leading bytes of message: %s\n", strerror(-r));
		goto out;
	}	
	r = sd_bus_message_read_array(msg, 'y', (const void **)&data, &data_sz);
	if (r < 0) {

		printf("Couldn't parse data bytes of message: %s\n", strerror(-r));
		goto out;
	}

	kcs_msg->call = sd_bus_message_ref(msg);
	if (kcs_msg->call < 0)
	fprintf(stderr,"message_ref failed\n");
	kcs_msg->rsp.netfn = netfn;
	kcs_msg->rsp.lun = lun;
	kcs_msg->rsp.seq = seq;
	kcs_msg->rsp.cmd = cmd;
	kcs_msg->rsp.cc = cc;
	kcs_msg->rsp.data_len = data_sz;
	/* Because we've ref'ed the msg, I hope we don't need to memcpy data */

	res_buf[0]=(netfn << 2 ) | (lun & 0x03) ;
	res_buf[1]=cmd;
	res_buf[2]=cc;
	memcpy(res_buf+3,data,data_sz);
	fd=get_fd();
	write(fd,res_buf,data_sz+3);
	if (kcs_msg->call) {
		r = sd_bus_message_new_method_return(kcs_msg->call, &msg);
		if (r < 0) {
				fprintf(stderr,"Couldn't create response message\n");
			goto out;
		}
		r = 0; /* Just to be explicit about what we're sending back */
		r = sd_bus_message_append(msg, "x", r);
		if (r < 0) {
				fprintf(stderr,"Couldn't append result to response\n");
			goto out;
		}

	}

	if (kcs_msg->call) {
		if (sd_bus_send(bus, msg, NULL) < 0)
				fprintf(stderr,"Couldn't send response message\n");
		sd_bus_message_unref(msg);
	}
out:	
	free(kcs_msg);
	cur_fd= get_cur_fd(fd);
	response[cur_fd] = 1;
	return r;
}



void  *kcs_thread (void *kcs_fd)  
{
	int fd=*(int *)kcs_fd;
	int i,r,len,cur_fd;
	struct timespec req;
	struct timespec rem;
	req.tv_sec = 0;
	req.tv_nsec = 10000000;
	char req_buf[REQ_BUFF_SIZE];	
	sd_bus_message *msg;
	struct ipmi_msg n={0};

	cur_fd= get_cur_fd(fd);

	while(1)
	{

		if( (len=read(fd, req_buf, sizeof(req_buf)) ) > 0 )	
		{
			enq(fd);
			response[cur_fd] = 0;
			n.netfn= req_buf[0] >> 2 ;
			n.lun  = req_buf[0] & 0x3 ;
			n.seq  = 0;
			n.cmd  = req_buf[1] ;
			n.data_len= len-2;
			if (n.data)
				free(n.data);
			n.data = malloc(len-2);
			if (n.data){
				n.data = memcpy(n.data, req_buf + 2, n.data_len);
			}else{
				fprintf(stderr,"Failed to allocate memory");
				goto out1_free;

				}


			r = sd_bus_message_new_signal(bus, &msg, OBJ_NAME, INTF_NAME, "ReceivedMessage");
			if (r < 0) {
				fprintf(stderr,"Failed to create signal: %s\n", strerror(-r));
				goto out1_free;
			}
			r = sd_bus_message_append(msg, "yyyy", n.seq, n.netfn, n.lun, n.cmd);
			if (r < 0)
			{
				fprintf(stderr,"Couldn't append to signal: %s\n", strerror(-r));
				goto out1_free;
			}
			r = sd_bus_message_append_array(msg, 'y',  n.data, n.data_len);
			if (r < 0) {
				fprintf(stderr,"Couldn't append array to signal: %s\n", strerror(-r));
				goto out1_free;
			}
			r = sd_bus_send(bus, msg, NULL);
			if (r < 0) {
				fprintf(stderr,"Couldn't emit dbus signal: %s\n", strerror(-r));
				goto out1_free;
			}
			sd_bus_message_unref(msg);

			for (;response[cur_fd]==0;) {
				r = sd_bus_process(bus, NULL);
				if (r < 0) {
					fprintf(stderr, "Failed to process bus: %s\n", strerror(-r));
					goto out1_free; 
				}
				if (r > 0) // we processed a request, try to process another one, right-away 
					continue;

				// Wait for the next request to process 
				r = sd_bus_wait(bus, 100000);
				if (r < 0) {
					fprintf(stderr, "Failed to wait on bus: %s\n", strerror(-r));
					goto out1_free; 
				} 
			}//for looop
		}
		else
			nanosleep(&req, &rem);
	}

out1_free : 
	sd_bus_message_unref(msg);
}

static const sd_bus_vtable ipmid_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("sendMessage", "yyyyyay", "x", &method_send_message_kcs, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_SIGNAL("ReceivedMessage", "yyyyay", 0),
	SD_BUS_VTABLE_END
};

int main(char argc , char **argv)
{     
	pthread_t thread;
	int i=0,r,len;
	char dev_name[10];

	kcs_dev_enable(); //KCS channels enabling

	pthread_mutex_init(&lock, NULL);
	r = sd_bus_default_system(&bus);
	if (r < 0) {
		fprintf(stderr, "Failed to connect to system bus: %s\n", strerror(-r));
		goto finish;
	}

	//	Registering dbus methods/signals
	r = sd_bus_add_object_vtable(bus,
			NULL,
			OBJ_NAME,
			INTF_NAME,
			ipmid_vtable,
			NULL);
	if (r < 0) {
		fprintf(stderr, "Failed to issue method call: %s\n", strerror(-r));
		goto finish;
	}

	//	Requesting dbus name 
	r = sd_bus_request_name(bus, DBUS_NAME, SD_BUS_NAME_ALLOW_REPLACEMENT|SD_BUS_NAME_REPLACE_EXISTING);
	if (r < 0) {
		fprintf(stderr, "Failed to acquire service name: %s\n", strerror(-r));
		goto finish;

	}
	//opening kcs devices
	for(i=0;i<TOTAL_FD;i++)
	{
		snprintf(dev_name,10,"/dev/kcs%d",i);
		kcs_fd[i]=open(dev_name,O_RDWR);
		if( kcs_fd[i] < 0 )
		{
			fprintf(stderr, "error opening /dev/kcs%d  dev file...\n",i);
			goto finish;

		}
		else
		{
			//threads creation for each channel
			pthread_create(&thread, NULL, kcs_thread,(void *)&kcs_fd[i]);
		}
	}

	pthread_join(thread,NULL);

finish:
	for(i--;i>=0;i--)
		close(kcs_fd[i]);
	sd_bus_unref(bus);
	return r;
}


