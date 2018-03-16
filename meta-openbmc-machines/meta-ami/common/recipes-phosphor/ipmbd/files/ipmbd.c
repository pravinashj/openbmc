#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdint.h>
#include <mqueue.h>
#include <semaphore.h>
#include <poll.h>
#include "ipmbd.h"
#include "i2c.h"

#define DEBUG 0

#define MAX_BYTES 300

#define MQ_IPMB_REQ "/mq_ipmb_req"
#define MQ_IPMB_RES "/mq_ipmb_res"
#define MQ_MAX_MSG_SIZE MAX_BYTES
#define MQ_MAX_NUM_MSGS 256

#define SEQ_NUM_MAX 64

#define I2C_RETRIES_MAX 15

#define IPMB_PKT_MIN_SIZE 6

#define WRITE_TIMEOUT       1
#define HHM_FAILURE                 ( -1 )
ssize_t i2c_master_write_on_fd(int ,unsigned char ,unsigned char *,size_t  );

ssize_t internal_master_write( int ,unsigned char ,unsigned char *,size_t ,bool  );


// Structure for sequence number and buffer
typedef struct _seq_buf_t {
  bool in_use; // seq# is being used
  uint8_t len; // buffer size
  uint8_t *p_buf; // pointer to buffer
  sem_t s_seq; // semaphore for thread sync.
} seq_buf_t;

// Structure for holding currently used sequence number and
// array of all possible sequence number
typedef struct _ipmb_sbuf_t {
  uint8_t curr_seq; // currently used seq#
  seq_buf_t seq[SEQ_NUM_MAX]; //array of all possible seq# struct.
} ipmb_sbuf_t;

// Global storage for holding IPMB sequence number and buffer
ipmb_sbuf_t g_seq;

// mutex to protect global data access
pthread_mutex_t m_seq;

pthread_mutex_t m_i2c;

static int g_bus_id = 0; // store the i2c bus ID for debug print
//static int g_payload_id = 1; // Store the payload ID we need to use


static int
i2c_open(uint8_t bus_num) {
  int fd;
  char fn[32];
  int rc;

syslog(LOG_WARNING,"inside i2c_opne\n");

  snprintf(fn, sizeof(fn), "/dev/i2c-%d", bus_num);
  fd = open(fn, O_RDWR);
  if (fd == -1) {
    syslog(LOG_WARNING, "Failed to open i2c device %s", fn);
    return -1;
  }

  rc = ioctl(fd, I2C_SLAVE, BRIDGE_SLAVE_ADDR);
  if (rc < 0) {
    syslog(LOG_WARNING, "Failed to open slave @ address 0x%x", BRIDGE_SLAVE_ADDR);
    close(fd);
    return -1;
  }

  return fd;
}

static int
i2c_write(int fd, uint8_t *buf, uint8_t len) {
  struct i2c_rdwr_ioctl_data data;
  struct i2c_msg msg;
  int rc;
  int i;


  memset(&msg, 0, sizeof(msg));

  msg.addr = buf[0] >> 1;
  msg.flags = 0;
  msg.len = len - 1; // 1st byte in addr
  msg.buf = &buf[1];

  data.msgs = &msg;
  data.nmsgs = 1;

  pthread_mutex_lock(&m_i2c);

  for (i = 0; i < I2C_RETRIES_MAX; i++) {
    rc = ioctl(fd, I2C_RDWR, &data);
    if ((rc == -1) )
	syslog(LOG_WARNING,"return value in i2c_write ioctl  ( ) - %x %s\n",errno, strerror(errno));

    if (rc < 0) {
      usleep(200);
      continue;
    } else {
      break;
    }
  }

  if (rc < 0) {
    syslog(LOG_WARNING, "bus: %d, Failed to do raw io", g_bus_id);
    pthread_mutex_unlock(&m_i2c);
    return -1;
  }

  pthread_mutex_unlock(&m_i2c);

  return 0;
}


/****************************************************** 
 *   Description: Function to handle IPMB request     *
 *   ipmb_req_handler: Thread to handle new requests  *
 *   bus_num	: Bus number			      *
 ******************************************************/
static void*
ipmb_req_handler(void *bus_num) {
  uint8_t *bnum = (uint8_t*) bus_num;
  mqd_t mq;
  int fd = 0;
  int i = 0;

  //Buffers for IPMB transport
  uint8_t rxbuf[MQ_MAX_MSG_SIZE] = {0};
  uint8_t txbuf[MQ_MAX_MSG_SIZE] = {0};
  ipmb_req_t *p_ipmb_req;
  ipmb_res_t *p_ipmb_res;

  p_ipmb_req = (ipmb_req_t*) rxbuf;
  p_ipmb_res = (ipmb_res_t*) txbuf;


  uint8_t rlen = 0;
  uint16_t tlen = 0;

  char mq_ipmb_req[64] = {0};

  sprintf(mq_ipmb_req, "%s_%d", MQ_IPMB_REQ, *bnum);

  // Open Queue to receive requests
  mq = mq_open(mq_ipmb_req, O_RDONLY);
  if (mq == (mqd_t) -1) {
    return NULL;
  }

  // Open the i2c bus for sending response
  fd = i2c_open(*bnum);
  if (fd < 0) {
    syslog(LOG_WARNING, "i2c_open failure\n");
    mq_close(mq);
    return NULL;
  }

  // Loop to process incoming requests
  while (1) {
    if ((rlen = mq_receive(mq, (char *)rxbuf, MQ_MAX_MSG_SIZE, NULL)) <= 0) {
      sleep(1);
      continue;
    }


#ifdef DEBUG
    syslog(LOG_WARNING, "Received Request of %d bytes\n", rlen);
    for (i = 0; i < rlen; i++) {
      syslog(LOG_WARNING, "0x%X", rxbuf[i]);
    }
	syslog(LOG_WARNING,"p_ipmb_req->res_slave_addr: %x \n",p_ipmb_req->res_slave_addr);
	syslog(LOG_WARNING,"p_ipmb_req->netfn_lun: %x \n",p_ipmb_req->netfn_lun);
	syslog(LOG_WARNING,"p_ipmb_req->hdr_cksum : %x \n",p_ipmb_req->hdr_cksum);
	syslog(LOG_WARNING,"p_ipmb_req->req_slave_addr: %x \n",p_ipmb_req->req_slave_addr);
	syslog(LOG_WARNING,"p_ipmb_req->seq_lun: %x \n",p_ipmb_req->seq_lun);
	syslog(LOG_WARNING,"p_ipmb_req->cmd:%x \n",p_ipmb_req->cmd);
#endif

    // Populate IPMB response data from IPMB request
    p_ipmb_res->req_slave_addr = p_ipmb_req->req_slave_addr;
    p_ipmb_res->res_slave_addr = p_ipmb_req->res_slave_addr;
    p_ipmb_res->cmd = p_ipmb_req->cmd;
    p_ipmb_res->seq_lun = p_ipmb_req->seq_lun;
	
    p_ipmb_res->netfn_lun = p_ipmb_req->netfn_lun;

    // Calculate Header Checksum
    p_ipmb_res->hdr_cksum = p_ipmb_res->req_slave_addr +
                           p_ipmb_res->netfn_lun;
    p_ipmb_res->hdr_cksum = ZERO_CKSUM_CONST - p_ipmb_res->hdr_cksum;

#ifdef DEBUG
    syslog(LOG_WARNING, "Sending Response of %d bytes\n", tlen+IPMB_HDR_SIZE-1);
    for (i = 0; i < tlen+IPMB_HDR_SIZE; i++) {
      syslog(LOG_WARNING, "0x%X:", txbuf[i]);
    }
#endif

     // Send response back
	int retval = 0;
	retval = i2c_master_write_on_fd(fd,txbuf[0] >> 1,&txbuf[1],0x06); 	
	if((retval == -1))
		syslog(LOG_WARNING,"errno:%x\t strerror(errno):%s \n",errno, strerror(errno));
  }
}


ssize_t i2c_master_write_on_fd( int i2cfd,unsigned char  slave,unsigned char *data, size_t count )
{
    /* Pass an actual in-use file descriptor to internal master write */
    return( internal_master_write( i2cfd, slave, data, count, true ) );
}

ssize_t internal_master_write( int i2cfd,unsigned char slave,unsigned char  *data,
                                      size_t count, bool do_wait )
{
    ssize_t ret = -1;
    int i =0;

    /* Check for bogus fd */
    if( i2cfd < 0 ){
	syslog(LOG_WARNING,"failed to open\n");
        return( HHM_FAILURE );}

#ifdef DEBUG
    syslog(LOG_WARNING, "Inside internal master write Sending Response of %d bytes\n");
    for (i = 0; i < count; i++) {
      syslog(LOG_WARNING, "0x%X:", data[i]);
    }
#endif

    /* Set the remote slave to which we'll be writing */
    if( ioctl( i2cfd, I2C_SLAVE, slave ) < 0 )
    {
        syslog(LOG_WARNING,"Cannot set remote slave device for master write\n" );
        return( HHM_FAILURE );
    }

    if( do_wait )
    {
        /* Wait for the device to be accessible for write */
        if( wait_for_write( i2cfd, WRITE_TIMEOUT ) < 0 )
        {
            syslog(LOG_WARNING,"Error waiting for bus to be writeable\n" );
            return( HHM_FAILURE );
        }
    }

    /* Write the specified data onto the bus */
    ret = write( i2cfd, data, count );

	if((ret == -1))
		syslog(LOG_WARNING,"inside writeerrno:%x\t strerror(errno):%s \n",errno, strerror(errno));

	
    if( (size_t)ret != count )
    {
        errno = EREMOTEIO;
        /*@=unrecog@*/
    }
    
    syslog(LOG_WARNING,"Write to I2C is Successfull\n");
    return( ret );
}

int wait_for_write( /*@unused@*/int i2cfd, /*@unused@*/int wait_time )
{

return 0;

}

/******************************************************
 *   Description: Function to handle IPMB response    *
 *   ipmb_res_handler: Thread to handle response      *
 *   bus_num    : Bus number                          *
 ******************************************************/
static void*
ipmb_res_handler(void *bus_num) {
  uint8_t *bnum = (uint8_t*) bus_num;
  uint8_t buf[MQ_MAX_MSG_SIZE] = { 0 };
  uint8_t len = 0;
  mqd_t mq;
  ipmb_res_t *p_res;
  uint8_t index;
  char mq_ipmb_res[64] = {0};

  sprintf(mq_ipmb_res, "%s_%d", MQ_IPMB_RES, *bnum);

  syslog(LOG_WARNING,"inside ipmb_res_handler\n");
  // Open the message queue
  mq = mq_open(mq_ipmb_res, O_RDONLY);
  if (mq == (mqd_t) -1) {
    syslog(LOG_WARNING, "mq_open fails\n");
    return NULL;
  }

  // Loop to wait for incomng response messages
  while (1) {
    if ((len = mq_receive(mq, (char *)buf, MQ_MAX_MSG_SIZE, NULL)) <= 0) {
      sleep(1);
      continue;
    }

    p_res = (ipmb_res_t *) buf;

  }
}


int
main(int argc, char * const argv[]) {
  pthread_t tid_req_handler;
  pthread_t tid_res_handler;
  uint8_t ipmb_bus_num;
  mqd_t mqd_req = (mqd_t)-1, mqd_res = (mqd_t)-1;
  struct mq_attr attr;
  char mq_ipmb_req[64] = {0};
  char mq_ipmb_res[64] = {0};
  int rc = 0;

  if (argc < 2) {
    syslog(LOG_WARNING, "ipmbd: Usage: ipmbd <bus#> ");
    exit(1);
  }

  ipmb_bus_num = (uint8_t)strtoul(argv[1], NULL, 0);
  g_bus_id = ipmb_bus_num;


  syslog(LOG_WARNING, "ipmbd: bus#:%d \n", ipmb_bus_num);

  pthread_mutex_init(&m_i2c, NULL);

  // Create Message Queues for Request Messages and Response Messages
  attr.mq_flags = 0;
  attr.mq_maxmsg = MQ_MAX_NUM_MSGS;
  attr.mq_msgsize = MQ_MAX_MSG_SIZE;
  attr.mq_curmsgs = 0;

  sprintf(mq_ipmb_req, "%s_%d", MQ_IPMB_REQ, ipmb_bus_num);
  sprintf(mq_ipmb_res, "%s_%d", MQ_IPMB_RES, ipmb_bus_num);

  // Remove the MQ if exists
  mq_unlink(mq_ipmb_req);

  mqd_req = mq_open(mq_ipmb_req, O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, &attr);
  if (mqd_req == (mqd_t) -1) {
    rc = errno;
    syslog(LOG_WARNING, "ipmbd: mq_open request failed errno:%d\n", rc);
    goto cleanup;
  }

  // Remove the MQ if exists
  mq_unlink(mq_ipmb_res);

  mqd_res = mq_open(mq_ipmb_res, O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, &attr);
  if (mqd_res == (mqd_t) -1) {
    rc = errno;
    syslog(LOG_WARNING, "ipmbd: mq_open response failed errno: %d\n", rc);
    goto cleanup;
  }

  // Create thread to handle IPMB Requests
  if (pthread_create(&tid_req_handler, NULL, ipmb_req_handler, (void*) &ipmb_bus_num) < 0) {
    syslog(LOG_WARNING, "ipmbd: pthread_create failed\n");
    goto cleanup;
  }

  // Create thread to handle IPMB Responses
  if (pthread_create(&tid_res_handler, NULL, ipmb_res_handler, (void*) &ipmb_bus_num) < 0) {
    syslog(LOG_WARNING, "ipmbd: pthread_create failed\n");
    goto cleanup;
  }


cleanup:
  if (tid_req_handler > 0) {
    pthread_join(tid_req_handler, NULL);
  }

  if (tid_res_handler > 0) {
    pthread_join(tid_res_handler, NULL);
  }

  if (mqd_res > 0) {
    mq_close(mqd_res);
    mq_unlink(mq_ipmb_res);
  }

  if (mqd_req > 0) {
    mq_close(mqd_req);
    mq_unlink(mq_ipmb_req);
  }

  pthread_mutex_destroy(&m_i2c);

  return 0;
}
