#include <stdio.h>      
#include <stdlib.h>     
#include <string.h>     
#include <time.h>       
#include <sys/types.h>  
#include <semaphore.h>  
#include <fcntl.h>      
#include <sys/mman.h>   
#include <sys/stat.h>   
#include <sys/ipc.h>    
#include <sys/msg.h>    
#include <unistd.h>     
#include <signal.h>     
#include <pthread.h>    
#include <sys/wait.h>   
#include <errno.h>      
#include <stdarg.h>   
// -------Enum--------- 
typedef enum {
    ATTACHED = 0,
    DETACHED = 1
} PMode; //Proccesin çalışma modunu tutuyor

typedef enum {
    CMD_START = 1,
    CMD_TERMINATE = 2
} PCommand; //Queueden gönderilen komutları tutuyor

typedef enum {
    Status_RUNNING = 0,
    Status_TERMINATED = 1
} PStatus; //Processin durumunu tutuyor 

// --------Struct---------
// Process bilgisi
typedef struct {
    pid_t pid;              // Process ID
    pid_t owner_pid;        // Hangi terminal başlattıysa onun PID'si
    char command[256];      // Çalıstırılan komut
    PMode mode;       // Attached / Detached
    PStatus status;   // Running / Terminated
    time_t start_time;      // Baslangic zamanı
    int is_active;          // 1 ise tablodaki slot dolu, 0 ise boş
} PInfo; // Her bir processin bilgilerini tutuyor

// Shared memory 
typedef struct {
    PInfo processes[50]; // 50 process bilgisi tutabilecek
    int process_count; // Toplam aktif process sayısını tutacak
} SharedData; 

// Mesaj yapisi
typedef struct {
    long msg_type;      // Mesaj tipi (queue filtresi icin)
    int command;        // CMD_START / CMD_TERMINATE
    pid_t sender_pid;   // Mesajı gönderen terminalin process'in PID'si
    pid_t target_pid;   // Hedef process PID (Hangi process ile ilgili olduğu)
} Message; //Mesaj queue'ya gönderilen mesajların yapısını tutuyor


//-----------Global Değişkenler------------
SharedData *shared_data = NULL; // Shared memory pointer'ı tabloyu Ramda tutuyor
sem_t *sem = NULL; // Semaphore pointer'ı aynı anda iki kişinin yada daha fazla kişinin tabloya erişmesini engelliyor
int mq_id = -1; //KUllanılan message queue id'si
int is_running = 1; //Threadlerin çalışıp çalışmayacağını kontrol ediyor kısaca flag görevi görüyor
pid_t instance_pid; //Terminalin process id'si
pthread_t monitor_tid; //Monitor thread id'si
pthread_t listener_tid; //IPC mesajlarını dinleyen thread'in ID'si


// ---------Fonksiyon Prototipleri---------
void start_process(void);
void add_process(pid_t pid, char *command, PMode mode);
void send_message(int command, pid_t target_pid);
char** parse_command(char *input);
void list_processes(void);
void terminate_process(void);
void* monitor_thread(void *arg);
void* ipc_listener_thread(void *arg);
void init_ipc(void);
void cleanup_ipc(void);
void menu_yazdir(void);
void dongu(void);
void log_msg(const char *tag, const char *fmt, ...);

// ---------Fonksiyonlar---------
// Formatlı şekilde log mesajları yazmak için kullanılıyor
void log_msg(const char *tag, const char *fmt, ...) {
    va_list args;
    printf("[%s] ", tag);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

// Girilen komutu örnek (Sleep 10 gibi) bunu argv formatına çevirir.
char** parse_command(char *input) {
    char **argv = malloc(10 * sizeof(char*));
    if (!argv) {
        log_msg("HATA", "Bellek ayirma hatasi.");
        return NULL;
    }

    int index = 0;
    char *token = strtok(input, " ");
    while (token != NULL && index < 9) {
        argv[index++] = token;
        token = strtok(NULL, " ");
    }
    argv[index] = NULL;
    return argv;
}

// Shared memory'ye yeni process ekler. Boş slot bulur ve bilgileri yazar ve count'u artırır.
void add_process(pid_t pid, char *command, PMode mode) {
    sem_wait(sem);

    for (int i = 0; i < 50; i++) {
        if (!shared_data->processes[i].is_active) {
            PInfo *proc = &shared_data->processes[i];
            proc->pid = pid;
            proc->owner_pid = instance_pid;
            strncpy(proc->command, command, sizeof(proc->command) - 1);
            proc->mode = mode;
            proc->status = Status_RUNNING;
            proc->start_time = time(NULL);
            proc->is_active = 1;
            shared_data->process_count++;
            sem_post(sem);
            log_msg("SUCCESS", "Process baslatildi: PID %d", pid);
            return;
        }
    }

    sem_post(sem);
    log_msg("HATA", "Uygun process yeri bulunamadi (maksimum 50)!");
}

// Mesaj kuyruğuna komut gönderir. Diğer terminaller komutu alır.
void send_message(int command, pid_t target_pid) {
    Message msg;
    msg.msg_type = 1;           
    msg.command = command;
    msg.sender_pid = instance_pid;
    msg.target_pid = target_pid;

    if (msgsnd(mq_id, &msg, sizeof(Message) - sizeof(long), 0) == -1) {
        log_msg("HATA", "Mesaj gonderme hatasi: %s", strerror(errno));
    }
}

// --------------Process-------------
// Kullanıcı komutu girince fork yapar ve child processte exec ile komutu çalıştırır. 
//Tabloya eklenir ve IPC ile diğer terminallere bildirilir.
void start_process(void) {
    char command[256];
    printf("Çalistirilacak komutu girin: ");
    if (fgets(command, sizeof(command), stdin) == NULL) {
        log_msg("HATA", "Gecersiz komut girdisi!");
        return;
    }

    command[strcspn(command, "\n")] = '\0'; // '\n' sil
    if (command[0] == '\0') {
        log_msg("HATA", "Bos komut girilemez!");
        return;
    }

    int mode_choice;
    printf("Mod secin (0: Attached, 1: Detached): ");
    if (scanf("%d", &mode_choice) != 1) {
        log_msg("HATA", "Gecersiz mod secimi!");
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        return;
    }

    int c;
    while ((c = getchar()) != '\n' && c != EOF);

    PMode mode = (mode_choice == 0) ? ATTACHED : DETACHED;

    char cmd_copy[256];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    char **argv = parse_command(cmd_copy);
    if (!argv || !argv[0]) {
        log_msg("HATA", "Komut parse edilemedi!");
        free(argv);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_msg("HATA", "fork hatasi: %s", strerror(errno));
        free(argv);
        return;
    }

    if (pid == 0) {
        if (mode == DETACHED) {
            if (setsid() == -1) {
                log_msg("HATA", "setsid hatasi: %s", strerror(errno));
            }
        }
        execvp(argv[0], argv);
        log_msg("HATA", "execvp hatasi: %s", strerror(errno));
        exit(1);
    } else {
        free(argv);
        add_process(pid, command, mode);
        send_message(CMD_START, pid);
        log_msg("INFO", "Yeni process baslatildi: PID %d, MODE=%s", pid, (mode == ATTACHED) ? "ATTACHED" : "DETACHED");
    }
}
// -----------Process Listeleme----------
// Shared memory'deki aktif processleri formatlı şekilde listeler.
void list_processes(void) {
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                    ÇALIŞAN PROGRAMLAR                         ║\n");
    printf("╠═══════════════════════════════════════════════════════════════╣\n");
    printf("║ PID     │ Command              │ Mode      │ Owner   │ Sure   ║\n");
    printf("╠═══════════════════════════════════════════════════════════════╣\n");

    sem_wait(sem);

    int active_count = 0;
    time_t now = time(NULL);

    for (int i = 0; i < 50; i++) {
        PInfo *p = &shared_data->processes[i];
        if (!p->is_active)
            continue;

        active_count++;

        const char *mode_str = (p->mode == ATTACHED) ? "Attached" : "Detached";
        int runtime = (int)(now - p->start_time);

        printf("║ %-7d │ %-20s │ %-8s │ %-7d │ %-5ds ║\n",
               p->pid,
               p->command,
               mode_str,
               p->owner_pid,
               runtime);
    }

    sem_post(sem);

    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("Toplam: %d process\n", active_count);
}
// -----------Process Sonlandırma----------
// Bir PID alır ve o process'e SIGTERM(kill) sinyali gönderir. Asıl silme işlemi burada yapılmaz.
void terminate_process(void) {
    pid_t target_pid;

    printf("Sonlandirilacak process PID: ");
    if (scanf("%d", &target_pid) != 1) {
        log_msg("HATA", "Gecersiz PID girdiniz!");
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        return;
    }

    int c;
    while ((c = getchar()) != '\n' && c != EOF);

    sem_wait(sem);

    int found = 0;
    for (int i = 0; i < 50; i++) {
        PInfo *p = &shared_data->processes[i];
        if (p->is_active && p->pid == target_pid) {
            found = 1;
            // statuyu burada TERMINATED yapmiyoruz;
            // monitor thread waitpid ile tespit edip guncelleyecek.
            break;
        }
    }

    sem_post(sem);

    if (!found) {
        log_msg("HATA", "PID bulunamadi veya aktif degil: %d", target_pid);
        return;
    }

    if (kill(target_pid, SIGTERM) == -1) {
        log_msg("HATA", "Process sonlandirilamadi (PID=%d): %s",
                target_pid, strerror(errno));
        return;
    }

    // Dokumanda istenen log:
    log_msg("INFO", "Process %d'e SIGTERM sinyali gonderildi", target_pid);

    // Diger instance'lara da bildir:
    send_message(CMD_TERMINATE, target_pid);
}
//------------Monitor Thread-----------
// Biten processleri tespit eder ve tablodan siler ve IPC ile diğer terminallere bildirir.

void* monitor_thread(void *arg) {
    (void)arg;

    while (is_running) {
        int status;
        pid_t ended_pid;

        // Tum child'lari non-blocking kontrol et
        while ((ended_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            sem_wait(sem);

            for (int i = 0; i < 50; i++) {
                PInfo *p = &shared_data->processes[i];
                if (p->is_active && p->pid == ended_pid) {
                    p->is_active = 0;
                    p->status = Status_TERMINATED;
                    if (shared_data->process_count > 0)
                        shared_data->process_count--;

                    sem_post(sem);

                    // Dokumanda istenen log:
                    log_msg("MONITOR", "Process %d sonlandi", ended_pid);

                    // Diger instance'lara bildir
                    send_message(CMD_TERMINATE, ended_pid);
                    goto next_loop;
                }
            }

            sem_post(sem);
        next_loop:
            ;
        }

        usleep(500000); // 0.5 saniye
    }

    return NULL;
}
// --------IPC Listener Thread--------
// Mesaj queueden gelen mesajları dinler ve işler.
void* ipc_listener_thread(void *arg) {
    (void)arg;
    Message msg;

    while (is_running) {
        ssize_t r = msgrcv(
            mq_id,
            &msg,
            sizeof(Message) - sizeof(long),
            0,              // tum tipler
            IPC_NOWAIT      // bloklamadan
        );

        if (r == -1) {
            if (errno == ENOMSG) {
                usleep(200000); // mesaj yoksa biraz bekle
                continue;
            } else {
                log_msg("HATA", "msgrcv hatasi: %s", strerror(errno));
                usleep(200000);
                continue;
            }
        }

        // Kendi gonderdigimiz mesajlari atla
        if (msg.sender_pid == instance_pid)
            continue;

        if (msg.command == CMD_START) {
            // Dokumandaki formata yakin
            log_msg("IPC", "Yeni process baslatildi: PID %d", msg.target_pid);
        } else if (msg.command == CMD_TERMINATE) {
            log_msg("IPC", "Process sonlandirildi: PID %d", msg.target_pid);
        } else {
            log_msg("IPC", "Bilinmeyen komut alindi: %d (Gonderen PID=%d)",
                    msg.command, msg.sender_pid);
        }
    }

    return NULL;
}
// --------IPC Başlatma ve Temizleme---------
// IPC bileşenlerini başlatır.
void init_ipc(void) {
    instance_pid = getpid();

    // 1) Shared memory
    int shm_fd = shm_open("/procx_shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        log_msg("HATA", "shm_open hatasi: %s", strerror(errno));
        exit(1);
    }

    if (ftruncate(shm_fd, sizeof(SharedData)) == -1) {
        log_msg("HATA", "ftruncate hatasi: %s", strerror(errno));
        exit(1);
    }

    shared_data = mmap(NULL, sizeof(SharedData),
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        log_msg("HATA", "mmap hatasi: %s", strerror(errno));
        exit(1);
    }

    // İlk kez aciliyorsa veya bozuksa sifirla
    if (shared_data->process_count < 0 || shared_data->process_count > 50) {
        memset(shared_data, 0, sizeof(SharedData));
    }

    // 2) Semaphore
    sem = sem_open("/procx_sem", O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        log_msg("HATA", "sem_open hatasi: %s", strerror(errno));
        exit(1);
    }

    // 3) Message Queue
    key_t key = ftok("/tmp", 'P');
    if (key == -1) {
        log_msg("HATA", "ftok hatasi: %s", strerror(errno));
        exit(1);
    }

    mq_id = msgget(key, IPC_CREAT | 0666);
    if (mq_id == -1) {
        log_msg("HATA", "msgget hatasi: %s", strerror(errno));
        exit(1);
    }

    log_msg("INFO", "IPC basariyla baslatildi. PID=%d", instance_pid);

    // Thread'leri baslat
    if (pthread_create(&monitor_tid, NULL, monitor_thread, NULL) != 0) {
        log_msg("HATA", "monitor thread olusturulamadi: %s", strerror(errno));
    }

    if (pthread_create(&listener_tid, NULL, ipc_listener_thread, NULL) != 0) {
        log_msg("HATA", "ipc listener thread olusturulamadi: %s", strerror(errno));
    }
}
// Program kapanırken IPC bileşenlerini temizler.
void cleanup_ipc(void) {
    is_running = 0;

    // Thread'leri bekle
    pthread_join(monitor_tid, NULL);
    pthread_join(listener_tid, NULL);

    // Bu instance'in ATTACHED process'lerini oldur
    sem_wait(sem);
    for (int i = 0; i < 50; i++) {
        PInfo *p = &shared_data->processes[i];
        if (p->is_active &&
            p->owner_pid == instance_pid &&
            p->mode == ATTACHED) {

            log_msg("CLEANUP", "Attached process sonlandiriliyor: PID %d", p->pid);
            kill(p->pid, SIGTERM);
            p->is_active = 0;
            p->status = Status_TERMINATED;
            if (shared_data->process_count > 0)
                shared_data->process_count--;
        }
    }
    sem_post(sem);

    // Shared memory detach
    if (shared_data != NULL) {
        munmap(shared_data, sizeof(SharedData));
    }

    // Semaphore kapat
    if (sem != NULL) {
        sem_close(sem);
    }

    // Message queue'yu (istege bagli) sil
    if (mq_id != -1) {
        if (msgctl(mq_id, IPC_RMID, NULL) == -1) {
            log_msg("HATA", "msgctl IPC_RMID hatasi: %s", strerror(errno));
        } else {
            log_msg("CLEANUP", "Message queue silindi.");
        }
    }

    log_msg("CLEANUP", "IPC temizligi tamamlandi.");
}

// ----------MENU----------
// Menü ekranını yazdırır.
void menu_yazdir(void) {
    printf("\n╔═══════════════════════════════╗\n");
      printf("║         ProcX v1.0            ║\n");
      printf("╠═══════════════════════════════╣\n");
      printf("║ 1. Yeni Program Çalistir      ║\n");
      printf("║ 2. Çalisan Programlari Listele║\n");
      printf("║ 3. Program Sonlandir          ║\n");
      printf("║ 0. Çikis                      ║\n");
      printf("╚═══════════════════════════════╝\n");
    printf("Seciminiz: ");
}
// Menüdeki seçenekleri yönetir.
void dongu(void) {
    int secenek;
    while (1) {
        menu_yazdir();
        if (scanf("%d", &secenek) != 1) {
            log_msg("HATA", "Gecersiz secim!");
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            continue;
        }

        int c;
        while ((c = getchar()) != '\n' && c != EOF);

        switch (secenek) {
            case 1:
                start_process();
                break;
            case 2:
                list_processes();
                break;
            case 3:
                terminate_process();
                break;
            case 0:
                log_msg("INFO", "Program kapatiliyor...");
                cleanup_ipc();
                log_msg("INFO", "Allah'a emanet olun!");
                exit(0);
            default:
                log_msg("HATA", "Gecersiz secim, tekrar deneyin.");
                break;
        }
    }
}

int main(void) {
    init_ipc();
    dongu();
    return 0;
}
