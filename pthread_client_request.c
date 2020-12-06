#include "data_global.h"
#include "uart_cache.h"
#include "sqlite_link_list.h"

extern unsigned char dev_led_mask;
extern unsigned char dev_camera_mask;
extern unsigned char dev_buzzer_mask;
extern unsigned char dev_uart_mask;

extern pthread_cond_t cond_led;
extern pthread_cond_t cond_camera;
extern pthread_cond_t cond_buzzer;
extern pthread_cond_t cond_refresh;
extern pthread_cond_t cond_uart_cmd;
extern pthread_cond_t cond_sqlite;

extern pthread_mutex_t mutex_global;
extern pthread_mutex_t mutex_uart_cmd;
extern pthread_mutex_t mutex_led;
extern pthread_mutex_t mutex_buzzer;
extern pthread_mutex_t mutex_camera;
extern pthread_mutex_t mutex_slinklist;

extern char cgi_status;
extern int msgid;
extern struct env_info_clien_addr all_info_RT; 

extern uart_cache_list m0_cache_head, m0_cache_tail;	//全局变量,所有线程都可访问(线程共享进程所有资源);这个到底是链表还是队列?
extern char recive_phone[12] ;
extern char center_phone[12] ;
struct setEnv
{
	int temMAX;
	int temMIN;
	int humMAX;
	int humMIN;
	int illMAX;
	int illMIN;
};



void *pthread_client_request (void *arg)	//与数据下行相关的线程入口函数
{
	key_t key;
	ssize_t msgsize;
	struct msg msgbuf;
	struct setEnv new;
	int sto_no;

	if ((key = ftok ("/app", 'g')) < 0)
	{
		perror ("ftok msgqueue");
		exit (-1);
	}
	if ((msgid = msgget (key, IPC_CREAT | IPC_EXCL | 0666)) < 0)
	{
		if(errno == EEXIST)
		{
			msgid = msgget (key,0666);
			return 0;
		} 
		else
		{
			perror ("msgget msgid");
			exit (-1);
		}
	}

	m0_cache_head = CreateEmptyCacheList ();
	m0_cache_tail = m0_cache_head;
	unsigned char *m0_temp;

	printf ("pthread_client_request is ok\n");
	while (1)
	{
		bzero (&msgbuf, sizeof (msgbuf));
		printf ("wait for the msg\n");
		msgsize = msgrcv (msgid, &msgbuf, sizeof (msgbuf) - sizeof (long), 1L, 0);
		printf ("Get %ldL msg\n", msgbuf.msgtype);
		printf ("text[0] = %#x\n", msgbuf.text[0]);

		switch (msgbuf.msgtype)
		{
		case 1L:
			{
				pthread_mutex_lock (&mutex_led);
				dev_led_mask = msgbuf.text[0];
				pthread_cond_signal (&cond_led);
				pthread_mutex_unlock (&mutex_led);
				break;
			}
		case 2L:
			{
				pthread_mutex_lock (&mutex_buzzer);
				dev_buzzer_mask = msgbuf.text[0];
				printf("msgbuf.text[0] = %d \n",msgbuf.text[0]);
				pthread_cond_signal (&cond_buzzer);
				pthread_mutex_unlock (&mutex_buzzer);
				break;
			}
		case MSG_CAMERA:
			{
				pthread_mutex_lock (&mutex_camera);
				dev_camera_mask = msgbuf.text[0];
				pthread_cond_signal (&cond_camera);
				pthread_mutex_unlock (&mutex_camera);
				break;
			}
		case 4L:
			{
				//usleep (200000);
				m0_temp = (unsigned char *)malloc (sizeof (unsigned char));
				*m0_temp = msgbuf.text[0];	//msgbuf.text[0]:命令字
				printf("msgbuf.text from pc : 0x%#x\n",msgbuf.text[0]);
				pthread_mutex_lock (&mutex_uart_cmd);	//临界区越小越好
				InsertCacheNode (&m0_cache_tail, m0_temp);	//向链表尾部插入命令字
				//dev_uart_mask = msgbuf.text[0];
				pthread_mutex_unlock (&mutex_uart_cmd);
				pthread_cond_signal (&cond_uart_cmd);
				break;
			}
		case 5L:
			{
				memcpy (&new, msgbuf.text + 1, 24);
				sto_no = msgbuf.text[0] - 48;
				printf ("sto_no = %d temMAX = %d, temMIN = %d, humMAX = %d, hunMIN = %d, illMAX = %d, illMIN = %d\n",
						sto_no, new.temMAX, new.temMIN, new.humMAX, new.humMIN, new.illMAX, new.illMIN);
				pthread_mutex_lock (&mutex_global);
				if (new.temMAX > 0 && new.temMAX > all_info_RT.storage_no[sto_no].temperatureMIN)
				{
					all_info_RT.storage_no[sto_no].temperatureMAX = new.temMAX;
			//		printf("new.temMAX =%d\n", new.temMAX);
				}
				if (new.temMIN > 0 && new.temMIN < all_info_RT.storage_no[sto_no].temperatureMAX)
				{
					all_info_RT.storage_no[sto_no].temperatureMIN = new.temMIN;
			//		printf("new.temMIN = %d\n", new.temMIN);
				}
				if (new.humMAX > 0 && new.humMAX > all_info_RT.storage_no[sto_no].humidityMIN)
				{
					all_info_RT.storage_no[sto_no].humidityMAX = new.humMAX;
			//		printf("new.humMAX = %d\n", new.humMAX);
				}
				if (new.humMIN > 0 && new.humMIN < all_info_RT.storage_no[sto_no].temperatureMAX)
				{
					all_info_RT.storage_no[sto_no].humidityMIN = new.humMIN;
			//		printf("new.humMIN = %d\n", new.humMIN);
				}
				if (new.illMAX > 0 && new.illMAX > all_info_RT.storage_no[sto_no].illuminationMIN)
				{
					all_info_RT.storage_no[sto_no].illuminationMAX = new.illMAX;
			//		printf("new.illMAX = %d\n", new.illMAX);
				}
				if (new.illMIN > 0 && new.illMIN < all_info_RT.storage_no[sto_no].illuminationMAX)
				{
					all_info_RT.storage_no[sto_no].illuminationMIN = new.illMIN;
			//		printf("new.illMIN = %d\n", new.illMIN);
				}
				pthread_mutex_lock (&mutex_slinklist);
				sqlite_InsertLinknode (ENV_UPDATE, all_info_RT, sto_no, 0);//0,0分别是仓库号和货物种类号
				pthread_mutex_unlock (&mutex_slinklist);
				pthread_cond_signal (&cond_sqlite);
				pthread_mutex_unlock (&mutex_global);
				pthread_cond_signal (&cond_refresh);
				break;
			}
#if 1
		case 10L:
			{
				int i = 0 , j = 0 ;
				for(i = 0  ; i < 11; i++)
				{
					recive_phone[i] = msgbuf.text[i]; 	
				}
				recive_phone[i] = '\0';
				printf("recive:%s\n",recive_phone);
				for(j = 0 ;msgbuf.text[i] != '\0' && j < 12; i++, j++)
				{
					center_phone[j] =  msgbuf.text[i];
				}
				center_phone[j] = '\0';
				printf("center:%s\n",center_phone);
				pthread_mutex_lock (&mutex_slinklist);
				sqlite_InsertLinknode (ENV_UPDATE, all_info_RT, sto_no, 0);//0,0分别是仓库号和货物种类号
				pthread_mutex_unlock (&mutex_slinklist);
				pthread_cond_signal (&cond_sqlite);
				break;
			}
#endif 
#if 1
		case 11L:
			{
				char tmp[QUEUE_MSG_LEN] ={0};
				strcpy(tmp,msgbuf.text);
				char tmp1[100] = {0};
				char tmp2[100] = {0};
				int i = 0, j= 0;
				char cmd = 'f';
				for(i = 0 ; ; i++)
				{
					if(tmp[i] == 'n' || tmp[i] == 'f')
					{
						cmd = tmp[i];
						break;
					}
					tmp1[i] = tmp[i];	
				}
				tmp1[i] = '\0';
				printf("tmp1 : %s\n",tmp1);
				i++;
				for(j=0 ; tmp[i] != '\0'; i++, j++)
				{
					tmp2[j] = tmp[i];
				}
				tmp2[j] = '\0';
				printf("tmp2 : %s\n",tmp2);
				if(cmd == 'n')
				{
					static int status = 0;
					char ifcon[100] = "ifconfig wlan0 ";
					strcat(ifcon,tmp1);
					printf("string : %s\n",ifcon);
					system(ifcon);
					char gw[100] = "route add default gw ";
					strcat(gw,tmp2);
					system(gw);
					printf("gw : %s\n",gw);
					if(status == 0)
					{
						status = 1;
						system("mkdir /var/run/wpa_supplicant -p");
						system("wpa_supplicant -B -iwlan0 -c /etc/wpa-psk-tkip.conf");
						printf("wifi first start is ok\n");
					}
					else
					{
						system("ifconfig wlan0 up");
						printf("wifi start success\n");
					}
				}
				else if(cmd == 'f')
				{
					system("ifconfig wlan0 down");
					printf("wifi stop success\n");
				}
				break;
			}
#endif 
		default : break;
		}
	}

}
