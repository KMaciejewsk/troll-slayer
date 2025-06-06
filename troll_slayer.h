#ifndef TROLL_SLAYER_H
#define TROLL_SLAYER_H

#include <unistd.h>

// ----- stale globalne -----
extern int N_ZABOJCOW;
extern int M_MIAST;
extern int CZAS_ODPOCZYNKU_MIASTA_LOGICZNY;

#define MAX_MIAST 100

// ----- typy komunikatow -----
typedef enum {
    WANT_ANY_CITY_REQ,
    OFFER_CITY_RESP,
    REQ_SPECIFIC_CITY,
    ACK_SPECIFIC_CITY,
    RELEASE_CITY
} TYP_KOMUNIKATU;

// ----- statusy miast -----
typedef enum {
    WOLNE,
    OCZEKUJE_NA_OFERTY, // wsm to po co to xd
    OCZEKUJE_NA_ACK_DLA_KONKRETNEGO,
    W_MIESCIE,
    ODPOCZYWA
} STATUS_MIASTA;

// ----- fazy ubiegania się o miasto -----
typedef enum {
    IDLE,
    CZEKAM_NA_OFERTY,
    WYBRALEM_OFERTE_CZEKAM_NA_ACK,
    W_MIESCIE_P_i // zmiana nazwy?
} FAZA_UBIEGANIA_SIE;

// ----- struktura dla otrzymanych ofert miast -----
typedef struct {
    int id_oferujacego;
    int zegar_oferujacego;
    int id_oferowanego_miasta;
    int zegar_dostepnosci_miasta_z_oferty;
} OTRZYMANA_OFERTA;

// ----- struktura dla odłożonych żądań (REQ_SPECIFIC_CITY) -----
typedef struct {
    int id_zglaszajacego;
    int zegar_zglaszajacego;
} ODLOZONE_REQ;

// ----- struktura ogólnego komunikatu -----
typedef struct {
    TYP_KOMUNIKATU typ;
    int id_nadawcy;
    int zegar_nadawcy_oryginalny;
    union {
        OTRZYMANA_OFERTA oferta;          // dla OFFER_CITY_RESP
        int id_miasta_req;                // dla REQ_SPECIFIC_CITY
        int id_miasta_ack;                // dla ACK_SPECIFIC_CITY
        struct {
            int id_zwolnionego_miasta;
            int zegar_do_kiedy_odpoczywa;
        } release_info;                   // dla RELEASE_CITY
    } payload;
} KOMUNIKAT;

// ----- Funkcje -----
void AktualizujZegar(int otrzymany_zegar);
void ZycieZabojcyMPI(int moj_rank_mpi);
void ObsluzOdlozoneZadaniaDlaMiasta(int id_miasta_do_sprawdzenia);

#endif // TROLL_SLAYER_H