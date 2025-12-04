# OS_Proje
# 🧠 ProcX --- Linux Süreç Yönetimi ve IPC Tabanlı Process Controller

ProcX, Linux tabanlı sistemlerde çalışan bir **çoklu-terminal süreç
yönetim aracı**dır.\
Aynı anda birden fazla terminalde çalışabilir ve ortak bir **shared
memory + semaphore + message queue** yapısı üzerinden:

-   Process başlatabilir\
-   Process sonlandırabilir\
-   Process listesini senkronize görebilir\
-   Diğer terminallerin başlattığı/bitirdiği işlemleri gerçek zamanlı
    öğrenebilir

Tamamen **C dili**, **POSIX API**, **shared memory**, **semaphore**,
**message queue**, **threads**, **signals**, **fork-exec** mekanizmaları
üzerine inşa edilmiştir.

------------------------------------------------------------------------

## 🚀 Özellikler

### 🔹 1. Process Yönetimi

-   Yeni process başlatma (fork + execvp)
-   Attached / Detached modda başlatma
-   Otomatik runtime hesaplama
-   Process sonlandırma (SIGTERM)

### 🔹 2. IPC --- Inter-Process Communication

-   Shared Memory (POSIX `shm_open`)
-   Semaphore (POSIX `sem_open`)
-   Message Queue (System V `msgget/msgsnd/msgrcv`)
-   Gerçek zamanlı senkronizasyon

### 🔹 3. Çoklu Terminal Desteği

ProcX aynı anda birden fazla terminalde çalışır:

-   Her terminal kendi processlerini yönetebilir
-   Diğer terminallerden gelen olayları canlı görür
-   Ortak tablo tüm terminaller için günceldir

### 🔹 4. Arka Plan Görevleri

-   **Monitor thread:** Biten processleri tespit eder, shared memory'den
    siler ve IPC ile duyurur\
-   **Listener thread:** Diğer terminallerden gelen mesajları dinler

------------------------------------------------------------------------

## 📦 Kullanılan Teknolojiler

  Teknoloji                         Kullanım
  --------------------------------- --------------------------------------
  `fork()`                          Yeni process oluşturma
  `execvp()`                        Komut çalıştırma
  `shared memory (shm_open)`        Process tablosu paylaşımı
  `semaphore (sem_open)`            Veri bütünlüğü
  `message queue (msgsnd/msgrcv)`   IPC bildirimleri
  `pthread`                         Monitor ve IPC dinleyici thread'leri
  `kill(), waitpid()`               Process sinyalleri ve takip

------------------------------------------------------------------------

## 📁 Proje Yapısı

    ProcX/
    ├── procx.c            # Tüm işlem motoru
    ├── README.md          # Açıklama (bu dosya)
    └── Makefile (istersen ekleyebilirim)

------------------------------------------------------------------------

## 🛠️ Derleme ve Çalıştırma

### 1) Derle

``` bash
gcc procx.c -o procx -pthread -lrt
```

### 2) Çalıştır

``` bash
./procx
```

### 3) Aynı anda başka terminalde de aç

``` bash
./procx
```

IPC yapısı sayesinde iki terminal birbiriyle haberleşecektir.

------------------------------------------------------------------------

## 🎮 Kullanım Menüsü

    =================================
    |         ProcX v1.0            |
    |===============================|
    | 1. Yeni Program Çalistir      |
    | 2. Çalisan Programlari Listele|
    | 3. Program Sonlandir          |
    | 0. Çikis                      |
    =================================

------------------------------------------------------------------------

## 🔧 Teknik Mimarisi

### **Shared Memory**

-   50 process slotlu tablo
-   Her process için:
    -   PID
    -   command
    -   owner_pid
    -   mode
    -   start_time
    -   status
    -   is_active

### **Semaphore**

Tabloyu aynı anda 2 terminalin bozmasını engeller.

### **Message Queue**

-   `CMD_START`
-   `CMD_TERMINATE`
-   Her terminal birbirini canlı takip eder.

### **Threads**

-   `monitor_thread`: waitpid ile biten child'ları yakalar\
-   `ipc_listener_thread`: mesaj kuyruğunu dinler

------------------------------------------------------------------------

## 📊 Örnek Process Listesi

    =================================================================
    |                       ÇALIŞAN PROGRAMLAR                      |
    |===============================================================|
    | PID     │ Command              │ Mode      │ Owner   │ Sure   |
    |===============================================================|
    | 4021    │ sleep 10             │ Detached  │ 3891    │ 5s     |
    | 4025    │ ping google.com      │ Attached  │ 3891    │ 2s     |
    =================================================================
    Toplam: 2 process

------------------------------------------------------------------------

## 🧹 Kapanışta Yapılan İşlemler

-   ATTACHED modda başlatılan tüm çocuk processler öldürülür
-   Shared memory detach edilir
-   Semaphore kapatılır + unlink
-   Message queue (IPC_RMID) ile kaldırılır
-   Thread'ler join edilir

------------------------------------------------------------------------

## 🧩 Geliştirici Notları

Bu proje, Linux üzerinde aşağıdaki konuları öğrenmek için mükemmel bir
örnektir:

-   IPC mimarileri\
-   Process yönetimi\
-   Thread senkronizasyonu\
-   Multi-terminal senaryolar\
-   POSIX sistem çağrıları

------------------------------------------------------------------------

## 📞 İletişim

Herhangi bir geliştirme, PR, issue veya soru için iletişime
geçebilirsin.

------------------------------------------------------------------------

## 📜 Lisans

MIT License.
