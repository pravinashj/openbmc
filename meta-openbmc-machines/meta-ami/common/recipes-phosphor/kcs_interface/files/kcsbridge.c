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
//#include <linux/bt-bmc.h>
#include <systemd/sd-bus.h>
#define DBUS_NAME "org.openbmc.HostIpmikcs"
#define INTF_NAME "org.openbmc.HostIpmi"
#define OBJ_NAME "/org/openbmc/HostIpmi/1"
#define TOTAL_FD 3
struct ipmi_msg {
	uint8_t netfn;
	uint8_t lun;
	uint8_t seq;
	uint8_t cmd;
	uint8_t cc; /* Only used on responses */
	uint8_t *data;
	size_t data_len;
}dummy;
struct kcs_queue {
	struct ipmi_msg req;
	struct ipmi_msg rsp;
	struct timespec start;
	int expired;
	sd_bus_message *call;
	struct kcs_queue *next;
};

int sd_bus_fd = 0;
struct pollfd fds[1];
void process_bus_request(sd_bus *bus);
struct kcsbridged_context {
	//	struct pollfd fds[TOTAL_FDS];
	struct sd_bus *bus;
	struct kcs_queue *bt_q;
};

int fd,flag;
struct sd_bus *bus;

unsigned char req_buf[256];
unsigned char res_buf[300];

int response; 
void  *kcs_thread (void *) ; 



void  *poll_sd_bus (void *p) 
{
	int r;
	flag=1;
	printf("In thread\n");
	sd_bus_fd = sd_bus_get_fd(bus);
	fds[0].events = POLLIN;
	int polled;
	while (1){
		polled = poll(fds, 1, 1000);
		if (polled == 0)
			continue;
		if (polled < 0) {
			r = -errno;
			printf("Error from poll(): %s\n", strerror(errno));
			exit(1);

		}
		process_bus_request(bus);

	}
}


int method_send_message_kcs(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error)
{

	printf("\n******\n**********\ninside METHOD_SEND_MESSGAE\n**************/\n");
	struct kcs_queue *kcs_msg;
	uint8_t *data; 
	size_t data_sz;
	uint8_t netfn, lun, seq, cmd, cc,i;
	/*
	 * Doesn't say it anywhere explicitly but it looks like returning 0 or
	 * negative is BAD...
	 */
	int r = 1;
	//data= alloc(sizeof(300));
	kcs_msg=malloc(sizeof(struct kcs_queue));
	r = sd_bus_message_read(msg, "yyyyy", &seq, &netfn, &lun, &cmd, &cc);
	if (r < 0) {
		printf("Couldn't parse leading bytes of message: %s\n", strerror(-r));
		//goto out;
	}	
	r = sd_bus_message_read_array(msg, 'y', (const void **)&data, &data_sz);
	if (r < 0) {

		printf("Couldn't parse data bytes of message: %s\n", strerror(-r));
	}

	///////////////////////////////////////////////
	kcs_msg->call = sd_bus_message_ref(msg);
	if (kcs_msg->call < 0)
		printf("message_ref failed\n");
	kcs_msg->rsp.netfn = netfn;
	kcs_msg->rsp.lun = lun;
	kcs_msg->rsp.seq = seq;
	kcs_msg->rsp.cmd = cmd;
	kcs_msg->rsp.cc = cc;
	kcs_msg->rsp.data_len = data_sz;
	/* Because we've ref'ed the msg, I hope we don't need to memcpy data */
	printf("ntf : %d lun %d seq %d cmd %d cc %d len %d\n", netfn,lun,seq,cmd,cc,data_sz);

	res_buf[0]=(netfn << 2 ) | (lun & 0x03) ;
	res_buf[1]=cmd;
	res_buf[2]=cc;
	for(i=0;i<data_sz;i++)
		printf("%x  %d \n",data[i],i);
	memcpy(res_buf+3,data,data_sz);
	write(fd,res_buf,data_sz+3);
	printf("Written back to bmc driver \n");
	if (kcs_msg->call) {
		r = sd_bus_message_new_method_return(kcs_msg->call, &msg);
		if (r < 0) {
			printf("Couldn't create response message\n");
			//       goto out;
		}
		r = 0; /* Just to be explicit about what we're sending back */
		r = sd_bus_message_append(msg, "x", r);
		if (r < 0) {
			printf("Couldn't append result to response\n");
			//        goto out;
		}

	}

	if (kcs_msg->call) {
		if (sd_bus_send(bus, msg, NULL) < 0)
			printf("Couldn't send response message\n");
		sd_bus_message_unref(msg);
	}

	response = 1;
	return r;
	
}

static const sd_bus_vtable ipmid_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("sendMessage", "yyyyyay", "x", &method_send_message_kcs, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_SIGNAL("ReceivedMessage", "yyyyay", 0),
	SD_BUS_VTABLE_END
};

int main(char argc,char **argv)
{     
	pthread_t thread;
	int i,r,len;
	char dev_name[10];
	struct kcsbridged_context *context; 
	const char *dest, *path;
	  struct timespec req;
	  struct timespec rem;
	  req.tv_sec = 0;
  	req.tv_nsec = 10000000;

	struct ipmi_msg n={0};
	sd_bus_message *msg;
	
	sleep(10);
	fd=open("/dev/kcs2",O_RDWR);
	if( fd < 0 )
	{
		printf("error opening driver file\n");
		exit(0);
	}
	r = sd_bus_default_system(&bus);
	if (r < 0) {
		printf("Failed to connect to system bus: %s\n", strerror(-r));
		//	goto finish;
	}

	printf("Registering dbus methods/signals\n");
	r = sd_bus_add_object_vtable(bus,
			NULL,
			OBJ_NAME,
			INTF_NAME,
			ipmid_vtable,
			NULL);
	if (r < 0) {
		printf("Failed to issue method call: %s\n", strerror(-r));
		//	goto finish;
	}

	printf("Requesting dbus name: %s\n", DBUS_NAME);
	r = sd_bus_request_name(bus, DBUS_NAME, SD_BUS_NAME_ALLOW_REPLACEMENT|SD_BUS_NAME_REPLACE_EXISTING);
	if (r < 0) {
		printf("Failed to acquire service name: %s\n", strerror(-r));
		//	goto finish;
	}

	//			pthread_create(&thread, NULL, poll_sd_bus,NULL);
	system("echo 1 > /sys/devices/platform/ahb/ahb\:apb/1e789000.kcs2/enable"); 
	while(1)
	{


		if( (len=read(fd, req_buf, sizeof(req_buf)) ) > 0 )	
		{
			printf("\n\n data read from driver........... %d\n",len);
			for(i=0;i<len;i++)
				printf("%x ",req_buf[i]);
			printf("\n\n\n");
			response = 0;
			n.netfn= req_buf[0] >> 2 ;
			n.lun  = req_buf[0] & 0x3 ;
			n.seq  = 0;
			n.cmd  = req_buf[1] ;
			n.data_len= len-2;
			if (n.data)
				free(n.data);
			n.data = malloc(len-2);
	                if (n.data)
        	                n.data = memcpy(n.data, req_buf + 2, n.data_len);

			printf("ntf : %x lun %x seq %x cmd %x \n", n.netfn,n.lun,n.seq,n.cmd);

			r = sd_bus_message_new_signal(bus, &msg, OBJ_NAME, INTF_NAME, "ReceivedMessage");
			if (r < 0) {
				//	MSG_ERR("Failed to create signal: %s\n", strerror(-r));
				printf("Failed to create signal: %s\n", strerror(-r));
			}
			printf("sent signal\n");
			r = sd_bus_message_append(msg, "yyyy", n.seq, n.netfn, n.lun, n.cmd);
			if (r < 0)
			{
				printf("Couldn't append to signal: %s\n", strerror(-r));
				//	goto out1_free;
			}
			r = sd_bus_message_append_array(msg, 'y',  n.data, n.data_len);
			if (r < 0) {

				printf("Couldn't append array to signal: %s\n", strerror(-r));
				//	goto out1_free;
			}
			r = sd_bus_send(bus, msg, NULL);
			if (r < 0) {
				printf("Couldn't emit dbus signal: %s\n", strerror(-r));
			}
			sd_bus_message_unref(msg);

			for (;response==0;) {
				r = sd_bus_process(bus, NULL);
				if (r < 0) {
					fprintf(stderr, "Failed to process bus: %s\n", strerror(-r));
					goto finish;
				}
				if (r > 0) // we processed a request, try to process another one, right-away 
					continue;

				// Wait for the next request to process 
				r = sd_bus_wait(bus, 100000);
				if (r < 0) {
					fprintf(stderr, "Failed to wait on bus: %s\n", strerror(-r));
					goto finish;
				} 
			}//for looop
		}
		else
			nanosleep(&req, &rem);
	}
finish : 

	exit(0);

}


void process_bus_request(sd_bus *bus)
{
	int r;
	/* Process requests */
	r = sd_bus_process(bus, NULL);
	if (r < 0) {
		fprintf(stderr, "Failed to process bus: %s\n", strerror(-r));
		//       goto finish;
	}

}	
