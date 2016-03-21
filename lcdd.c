#include <wiringPi.h>
#include <lcd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>

#define IPC_FILENAME "/usr/sbin/lcdd"
#define IPC_PROJ_ID 42

#define BRIGHTNESS_DEFAULT 50
#define BRIGHTNESS_PERC_TO_RAW(PERC) ( ((double)PERC/100.)*700. )

#define LCD_LINES 2
#define LCD_ROWS 16

//Has to be 1, because pin 1 is the only hardware PWM pin available
#define BRIGHTNESS_PIN 1
#define LCD_RS_PIN 11
#define LCD_E_PIN 10
#define LCD_D0_PIN 0
#define LCD_D1_PIN 4
#define LCD_D2_PIN 2
#define LCD_D3_PIN 3

char pauseCharBits[8] = {
    0b00000,
    0b11011,
    0b11011,
    0b11011,
    0b11011,
    0b11011,
    0b00000,
    0b00000
};
char playCharBits[8] = {
    0b10000,
    0b11000,
    0b11100,
    0b11110,
    0b11100,
    0b11000,
    0b10000,
    0b00000
};
char wifiCharBits[5][8] = {{
    0b00000,
    0b10001,
    0b01010,
    0b00100,
    0b01010,
    0b10001,
    0b00000,
    0b00000
},{
    0b00000,
    0b00001,
    0b00011,
    0b00000,
    0b00000,
    0b00000,
    0b00000,
    0b00000
},{
    0b00000,
    0b00001,
    0b00011,
    0b00111,
    0b00000,
    0b00000,
    0b00000,
    0b00000
},{
    0b00000,
    0b00001,
    0b00011,
    0b00111,
    0b01111,
    0b00000,
    0b00000,
    0b00000
},{
    0b00000,
    0b00001,
    0b00011,
    0b00111,
    0b01111,
    0b11111,
    0b00000,
    0b00000

}};
#define PAUSE_CHAR 0
#define PLAY_CHAR 1
#define WIFI_CHAR(level) (PLAY_CHAR+1+level)

typedef struct _scrollThreadData scrollThreadData;
struct _scrollThreadData
{
    const char* line1;
    const char* line2;
};

unsigned int brightness_value;
int lcd_fd;
char scrollText[LCD_LINES][LCD_ROWS+1];
pthread_mutex_t scrollTextMutex[2];
bool stopThread = true;
pthread_t thread = NULL;
void *scrollThreadWorker(void* arg);
/* LCD HANDLING */
int scrollWrite()
{
    //lcdClear(lcd_fd);
    lcdPosition(lcd_fd, 0, 0);
    pthread_mutex_lock(&scrollTextMutex[0]);
    lcdPuts(lcd_fd, scrollText[0]);
    pthread_mutex_unlock(&scrollTextMutex[0]);

    lcdPosition(lcd_fd, 0, 1);
    pthread_mutex_lock(&scrollTextMutex[1]);
    lcdPuts(lcd_fd, scrollText[1]);
    pthread_mutex_unlock(&scrollTextMutex[1]);
    return 0;
}

void scrollTextSetCenter(const char* line1, const char* line2)
{
    bool startThread = false;
    scrollThreadData *data = malloc(sizeof(scrollThreadData));
    data->line1 = NULL;
    data->line2 = NULL;
    if (!stopThread) {
        stopThread = true;
        pthread_join(thread, NULL);
    }
    if  (line1 == NULL) {
        pthread_mutex_lock(&scrollTextMutex[0]);
        memset(scrollText[0], ' ', 16);
        pthread_mutex_unlock(&scrollTextMutex[0]);
    } else if (strlen(line1) < 16) {
        unsigned int offset = (16-strlen(line1))/2;
        pthread_mutex_lock(&scrollTextMutex[0]);
        memset(scrollText[0], ' ', offset);
        strncpy(scrollText[0] + offset, line1, strlen(line1));
        memset(scrollText[0] + offset + strlen(line1), ' ', 16 - offset - strlen(line1));
        scrollText[0][16] = '\0'; //ensure null-termination
        pthread_mutex_unlock(&scrollTextMutex[0]);
    } else {
        //start thread
        startThread = true;
        data->line1 = line1;
    }

    if (line2 == NULL) {
        pthread_mutex_unlock(&scrollTextMutex[1]);
        memset(scrollText[1], ' ', 16);
        pthread_mutex_unlock(&scrollTextMutex[1]);
    } else if (strlen(line2) < 16) {
        unsigned int offset = (16-strlen(line2))/2;
        pthread_mutex_unlock(&scrollTextMutex[1]);
        memset(scrollText[1], ' ', offset);
        strncpy(scrollText[1] + offset, line2, strlen(line2));
        memset(scrollText[1] + offset + strlen(line2), ' ', 16 - offset - strlen(line2));
        scrollText[1][16] = '\0'; //ensure null-termination
        pthread_mutex_unlock(&scrollTextMutex[1]);
    } else {
        //start thread
        startThread = true;
        data->line2 = line2;
    }

    if (startThread) {
        stopThread = false;
        pthread_create(&thread, NULL, scrollThreadWorker, (void*)data);
    } else {
        scrollWrite();
    }
}

int scrollInit()
{
    pthread_mutex_init(&scrollTextMutex[0], NULL);
    pthread_mutex_init(&scrollTextMutex[1], NULL);
    lcd_fd = lcdInit(LCD_LINES,
                     LCD_ROWS,
                     4, //Buswidth
                     LCD_RS_PIN,
                     LCD_E_PIN,
                     LCD_D0_PIN,
                     LCD_D1_PIN,
                     LCD_D2_PIN,
                     LCD_D3_PIN,
                     0, 0, 0, 0);
    lcdClear(lcd_fd);
    lcdCharDef(lcd_fd, PAUSE_CHAR, pauseCharBits);
    lcdCharDef(lcd_fd, PLAY_CHAR, playCharBits);
    lcdCharDef(lcd_fd, WIFI_CHAR(0),wifiCharBits[0]);
    lcdCharDef(lcd_fd, WIFI_CHAR(1),wifiCharBits[1]);
    lcdCharDef(lcd_fd, WIFI_CHAR(2),wifiCharBits[2]);
    lcdCharDef(lcd_fd, WIFI_CHAR(3),wifiCharBits[3]);
    lcdCharDef(lcd_fd, WIFI_CHAR(4),wifiCharBits[4]);

    return 0;
}

#define WAIT_TICK_CNT 5
void *scrollThreadWorker(void* arg)
{
    scrollThreadData *data = (scrollThreadData*) arg;
    size_t line1_len = (data->line1 ? strlen(data->line1) : 0);
    size_t line2_len = (data->line2 ? strlen(data->line2) : 0);
    size_t step1 = 0;
    size_t step2 = 0;
    unsigned int wait1 = 0;
    unsigned int wait2 = 0;
    while (!stopThread) {
        if (data->line1) {
            if (wait1 != 0) {
                --wait1;
            } else if (step1 > line1_len-16) {
                step1 = 0;
                wait1 = WAIT_TICK_CNT;
                pthread_mutex_lock(&scrollTextMutex[0]);
                strncpy(scrollText[0], data->line1, 16);
                pthread_mutex_unlock(&scrollTextMutex[0]);
            } else {
                pthread_mutex_lock(&scrollTextMutex[0]);
                strncpy(scrollText[0], data->line1 + step1, 16);
                pthread_mutex_unlock(&scrollTextMutex[0]);
               // if (step1+16 > line1_len)
               //     memset(scrollText[0]+(line1_len-step1), ' ', (16+step1-line1_len));
                if (++step1 > line1_len-16)
                    wait1=WAIT_TICK_CNT;
            }
        }

        if (data->line2) {
            if (wait2 != 0) {
                --wait2;
            } else if (step2 > line2_len-16) {
                step2 = 0;
                wait2 = WAIT_TICK_CNT;
                pthread_mutex_lock(&scrollTextMutex[1]);
                strncpy(scrollText[1], data->line2, 16);
                pthread_mutex_unlock(&scrollTextMutex[1]);

            } else {
                pthread_mutex_lock(&scrollTextMutex[1]);
                strncpy(scrollText[1], data->line2 + step2, 16);
                pthread_mutex_unlock(&scrollTextMutex[1]);
                if (++step2 > line2_len-16)
                    wait2=WAIT_TICK_CNT;
            }
        }
        scrollWrite();

        delay(1000);
    }
    free(data);
    pthread_exit(NULL);
}
/* BRIGHTNESS CONTROL */
int brightnessInit()
{
    brightness_value = BRIGHTNESS_DEFAULT;
    pinMode(BRIGHTNESS_PIN, PWM_OUTPUT);
    pwmWrite(BRIGHTNESS_PIN, BRIGHTNESS_PERC_TO_RAW(BRIGHTNESS_DEFAULT));
    return 0;
}

int brightnessSet(int percentage)
{
    brightness_value = percentage;
    pwmWrite(BRIGHTNESS_PIN, BRIGHTNESS_PERC_TO_RAW(percentage));
    return 0;
}

int brightnessFade(int endVal)
{
    if (brightness_value > endVal) {
        for(int i = brightness_value-1; i >= endVal; --i) {
            brightnessSet(i);
            delay(10);
        }
    }
    else if (brightness_value < endVal) {
        for(int i = brightness_value+1; i <= endVal; ++i) {
            brightnessSet(i);
            delay(10);
        }
    }
}

/* IPC HANDLING */
#define IPC_SIZE_MSG 1
#define IPC_STR_MSG 2
int read_msg(int msgqid, char **data, unsigned int *length)
{
    int status = -1;
    struct size_msg {
        long mtype;
        unsigned int length;
    } size_m;

    if (msgrcv(msgqid, &size_m, sizeof(int), IPC_SIZE_MSG, 0) != sizeof(int)) {
        return -1;
    }
    struct msg {
        long mtype;
        char data[ 1 ];
    } *m = (struct msg *)malloc( size_m.length + offsetof( struct msg, data ));

    if( m != NULL ) {
        if( msgrcv( msgqid, m, size_m.length, IPC_STR_MSG, 0 ) == size_m.length ) {
            *data = malloc(size_m.length);
            memcpy( *data, m->data, size_m.length );
            *length = size_m.length;
            status = 0;
        }
    }

    free( m );

    return status;
}

int msgqid;

int ipcInit()
{
    key_t key;

    if ((key = ftok(IPC_FILENAME, IPC_PROJ_ID)) == -1)
        return -1;

    if ((msgqid = msgget(key, (IPC_CREAT | IPC_EXCL | 0666) )) == -1) {
        if ((msgqid = msgget(key, 0 )) != -1) {
            fprintf(stderr, "msgget failed. maybe message queue already exists\n");
            if (msgctl(msgqid, IPC_RMID, NULL) == -1) {
                fprintf(stderr, "failed to get already present message queue\n");
                return -1;
            }
            if ((msgqid = msgget(key, (IPC_CREAT | IPC_EXCL | 0666) )) == -1) {
                fprintf(stderr, "retry to msgget after delete failed.\n");
                return -1;
            }
        }
    }
}

int ipcReceiveDouble()
{
    char* data = NULL;
    unsigned int length = 0;
    while (1) {
        if (read_msg(msgqid, &data, &length) ==  0) {
            if (length > strlen(data)) {
                printf("received msg: '%s', '%s' (%u)\n", data, data+strlen(data)+1, length);
                scrollTextSetCenter(data, data+strlen(data)+1);
            } else {
                printf("only single line was received!\n");
            }
        } else {
            printf("failed to receive from queue!\n");
        }
    }
    return 0;
}

int ipcReceiveSingle()
{
    char* data = NULL;
    unsigned int length = 0;
    while (1) {
        if (read_msg(msgqid, &data, &length) ==  0) {
            printf("received msg: '%s' (%u)\n", data, length);
            scrollTextSetCenter(data, NULL);
            scrollWrite();
        } else {
            printf("failed to receive from queue!\n");
        }
    }
    return 0;
}

/* MAIN */
int main(int argc, const char* argv[])
{

    if (wiringPiSetup() < 0) {
	    fprintf(stderr, "wiringPi setup failed.\n");
        exit(1);
    }
    if (brightnessInit() < 0) {
        fprintf(stderr, "brightness init failed.\n");
        exit(1);
    }
    if (scrollInit() < 0) {
        fprintf(stderr, "scroll init failed.\n");
        exit(1);
    }
    if (ipcInit() < 0) {
        fprintf(stderr, "ipc init failed.\n");
        exit(1);
    }
    scrollTextSetCenter("Hello", "World!");
    lcdPosition(lcd_fd, 0, 0);
    lcdPutchar(lcd_fd, PAUSE_CHAR);
    lcdPutchar(lcd_fd, PLAY_CHAR);

//    delay(5000);
//    scrollTextSetCenter("A very long teststring!.", "Not quite as long but still...");
//   delay(5000);
    ipcReceiveDouble();
    pthread_exit(NULL);
}
