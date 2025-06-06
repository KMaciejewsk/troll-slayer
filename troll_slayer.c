#include "troll_slayer.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>
#include <mpi.h>

// Stałe pomocnicze
#define NIESKONCZONOSC 99999999
#define PUSTA_LISTA 0

// ----- stale globalne -----
int N_ZABOJCOW;
int M_MIAST;
int CZAS_ODPOCZYNKU_MIASTA_LOGICZNY;

// ----- struktury dla kazdego procesu -----
static int moj_rank_mpi;
static int zegar_lamporta = 0;

// ----- lokalna wiedza o miastach -----
static STATUS_MIASTA status_miasta[MAX_MIAST];
static int miasto_odpoczywa_do_zegara[MAX_MIAST];

// ----- zmienne dotyczace ubiegania sie o miasto -----
static FAZA_UBIEGANIA_SIE aktualna_faza_ubiegania_sie;
#define MAX_OFERTY 100
static OTRZYMANA_OFERTA otrzymane_oferty_miast[MAX_OFERTY];
static int liczba_otrzymanych_ofert = 0;

static int wybrane_miasto_do_REQ;
static int zegar_mojego_REQ_o_konkretne_miasto;
static int licznik_otrzymanych_ACK_dla_konkretnego_miasta;

// ----- zmienne dotyczace odlozonych zadan -----
#define MAX_ODLOZONE_REQ 100
static ODLOZONE_REQ odlozone_REQ_dla_miasta[MAX_MIAST][MAX_ODLOZONE_REQ];
static int liczba_odlozonych_REQ_dla_miasta[MAX_MIAST];

// ----- funkcje -----
void WYPISZ_LOG(int rank, int clock, const char* message) {
    printf("[Zabojca %d, Zegar: %d] %s\n", rank, clock, message);
}

void WYPISZ_LOG_FORMATTED(int rank, int clock, const char* format, ...) {
    va_list args;
    va_start(args, format);
    printf("[Zabojca %d, Zegar: %d] ", rank, clock);
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void CZEKAJ(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

void ROZESLIJ_DO_WSZYSTKICH_INNYCH(TYP_KOMUNIKATU typ, int nadawca_rank, int zegar_nadawcy_oryginalny, ...) {
    KOMUNIKAT msg;
    msg.typ = typ;
    msg.id_nadawcy = nadawca_rank;
    msg.zegar_nadawcy_oryginalny = zegar_nadawcy_oryginalny;

    va_list args;
    va_start(args, zegar_nadawcy_oryginalny);

    if (typ == REQ_SPECIFIC_CITY) {
        msg.payload.id_miasta_req = va_arg(args, int);
    } else if (typ == RELEASE_CITY) {
        msg.payload.release_info.id_zwolnionego_miasta = va_arg(args, int);
        msg.payload.release_info.zegar_do_kiedy_odpoczywa = va_arg(args, int);
    }
    va_end(args);

    for (int i = 0; i < N_ZABOJCOW; ++i) {
        if (i == nadawca_rank) continue;
        MPI_Send(&msg, sizeof(KOMUNIKAT), MPI_BYTE, i, typ, MPI_COMM_WORLD);
    }
}

void WYSLIJ_DO(int odbiorca_rank, TYP_KOMUNIKATU typ, int nadawca_rank, int zegar_nadawcy_oryginalny, ...) {
    KOMUNIKAT msg;
    msg.typ = typ;
    msg.id_nadawcy = nadawca_rank;
    msg.zegar_nadawcy_oryginalny = zegar_nadawcy_oryginalny;

    va_list args;
    va_start(args, zegar_nadawcy_oryginalny);

    if (typ == OFFER_CITY_RESP) {
        msg.payload.oferta.id_oferowanego_miasta = va_arg(args, int);
        msg.payload.oferta.zegar_dostepnosci_miasta_z_oferty = va_arg(args, int);
        msg.payload.oferta.id_oferujacego = nadawca_rank;
        msg.payload.oferta.zegar_oferujacego = zegar_nadawcy_oryginalny;
    } else if (typ == ACK_SPECIFIC_CITY) {
        msg.payload.id_miasta_ack = va_arg(args, int);
    }
    va_end(args);

    MPI_Send(&msg, sizeof(KOMUNIKAT), MPI_BYTE, odbiorca_rank, typ, MPI_COMM_WORLD);
}

KOMUNIKAT ODBIERZ_KOMUNIKAT_NIEBLOKUJACY() {
    KOMUNIKAT msg;
    MPI_Status status;
    int flag;

    MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
    if (flag) {
        MPI_Recv(&msg, sizeof(KOMUNIKAT), MPI_BYTE, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, &status);
        return msg;
    } else {
        msg.typ = (TYP_KOMUNIKATU)(-1);
        return msg;
    }
}

void DODAJ_DO_LISTY_OFERT(OTRZYMANA_OFERTA oferta) {
    if (liczba_otrzymanych_ofert < MAX_OFERTY) {
        otrzymane_oferty_miast[liczba_otrzymanych_ofert++] = oferta;
    }
    else {
        WYPISZ_LOG(moj_rank_mpi, zegar_lamporta, "Brak miejsca na nowe oferty!");
    }
}

void DODAJ_DO_LISTY_ODLOZONEJ_REQ(int id_miasta, ODLOZONE_REQ req) {
    if (liczba_odlozonych_REQ_dla_miasta[id_miasta] < MAX_ODLOZONE_REQ) {
        odlozone_REQ_dla_miasta[id_miasta][liczba_odlozonych_REQ_dla_miasta[id_miasta]++] = req;
    }
    else {
        WYPISZ_LOG(moj_rank_mpi, zegar_lamporta, "Brak miejsca na nowe odlozone zlecenia!");
    }
}

int OTRZYMANO_WYSTARCZAJACO_OFERT() {
    return liczba_otrzymanych_ofert >= (N_ZABOJCOW / 2); // ile ofert proces musi otrzymac zeby kontynuowac, to mozna zmienic
}

// ----- zarzadzanie czasem -----
static clock_t timeout_start_time;
static int timeout_duration_ms = 5000; // 5 sekund

void USTAW_TIMEOUT_NA_OFERTY() {
    timeout_start_time = clock();
}

int MINAL_TIMEOUT_NA_OFERTY() {
    return ((int)((clock() - timeout_start_time) * 1000 / CLOCKS_PER_SEC)) > timeout_duration_ms;
}

// ----- funkcja akutalizacji zegara Lamporta -----
void AktualizujZegar(int otrzymany_zegar) {
    zegar_lamporta = (zegar_lamporta > otrzymany_zegar ? zegar_lamporta : otrzymany_zegar) + 1;
}

// ----- funkcja obsługi odłożonych żądań - POPRAWIONA -----
void ObsluzOdlozoneZadaniaDlaMiasta(int id_miasta_do_sprawdzenia) {
    ODLOZONE_REQ NOWA_LISTA_ODLOZONYCH_REQ[MAX_ODLOZONE_REQ];
    int nowa_liczba_odlozonych_REQ = 0;

    for (int i = 0; i < liczba_odlozonych_REQ_dla_miasta[id_miasta_do_sprawdzenia]; ++i) {
        ODLOZONE_REQ odlozonego_req = odlozone_REQ_dla_miasta[id_miasta_do_sprawdzenia][i];
        int czy_wyslac_ack_teraz = 0;

        // POPRAWIONA LOGIKA: Wysyłaj ACK tylko jeśli:
        // 1. Miasto jest wolne I nie ubiegam się o nie, LUB
        // 2. Ubiegam się o to miasto, ale drugi proces ma wyższy priorytet

        if (status_miasta[id_miasta_do_sprawdzenia] == WOLNE &&
            (aktualna_faza_ubiegania_sie != WYBRALEM_OFERTE_CZEKAM_NA_ACK || wybrane_miasto_do_REQ != id_miasta_do_sprawdzenia)) {
            // Miasto wolne i nie ubiegam się o nie - mogę wysłać ACK
            czy_wyslac_ack_teraz = 1;
        } 
        else if (aktualna_faza_ubiegania_sie == WYBRALEM_OFERTE_CZEKAM_NA_ACK && 
                 wybrane_miasto_do_REQ == id_miasta_do_sprawdzenia) {
            // Ubiegam się o to miasto - sprawdź priorytet według Ricarta-Agrawali
            if (zegar_mojego_REQ_o_konkretne_miasto > odlozonego_req.zegar_zglaszajacego ||
                (zegar_mojego_REQ_o_konkretne_miasto == odlozonego_req.zegar_zglaszajacego && 
                 moj_rank_mpi > odlozonego_req.id_zglaszajacego)) {
                // Drugi proces ma wyższy priorytet - wysyłam ACK
                czy_wyslac_ack_teraz = 1;
            }
            // Inaczej - mam wyższy priorytet, więc nadal odkładam
        }
        // Jeśli jestem w mieście (W_MIESCIE) - zawsze odkładam

        if (czy_wyslac_ack_teraz) {
            zegar_lamporta++;
            WYSLIJ_DO(odlozonego_req.id_zglaszajacego, ACK_SPECIFIC_CITY, moj_rank_mpi, zegar_lamporta, id_miasta_do_sprawdzenia);
            WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Wysyłam odłożony ACK do %d dla miasta %d", 
                                odlozonego_req.id_zglaszajacego, id_miasta_do_sprawdzenia);
        } else {
            NOWA_LISTA_ODLOZONYCH_REQ[nowa_liczba_odlozonych_REQ++] = odlozonego_req;
        }
    }
    
    memcpy(odlozone_REQ_dla_miasta[id_miasta_do_sprawdzenia], NOWA_LISTA_ODLOZONYCH_REQ, 
           nowa_liczba_odlozonych_REQ * sizeof(ODLOZONE_REQ));
    liczba_odlozonych_REQ_dla_miasta[id_miasta_do_sprawdzenia] = nowa_liczba_odlozonych_REQ;
}

// ----- glowna funkcja procesu -----
void ZycieZabojcyMPI(int rank_mpi) {
    moj_rank_mpi = rank_mpi;
    zegar_lamporta = 0;

    // ---- inicjalizacja ----
    for (int m = 0; m < M_MIAST; ++m) {
        status_miasta[m] = WOLNE;
        miasto_odpoczywa_do_zegara[m] = 0;
        liczba_odlozonych_REQ_dla_miasta[m] = PUSTA_LISTA;
    }
    aktualna_faza_ubiegania_sie = IDLE;

    srand(time(NULL) + moj_rank_mpi);

    // ---- główna pętla ----
    while (1) {
        // ---- faza 1: decyzja o ubieganiu się o miasto ----
        if (aktualna_faza_ubiegania_sie == IDLE) {
            WYPISZ_LOG(moj_rank_mpi, zegar_lamporta, "Rozmyślam...");
            CZEKAJ(rand() % 1000 + 500); // losowy_czas rozmyslania

            zegar_lamporta++;
            WYPISZ_LOG(moj_rank_mpi, zegar_lamporta, "Chcę odwiedzić jakiekolwiek miasto.");
            ROZESLIJ_DO_WSZYSTKICH_INNYCH(WANT_ANY_CITY_REQ, moj_rank_mpi, zegar_lamporta);
            aktualna_faza_ubiegania_sie = CZEKAM_NA_OFERTY;
            liczba_otrzymanych_ofert = PUSTA_LISTA;
            USTAW_TIMEOUT_NA_OFERTY();
        }

        // ---- faza 2: oczekiwanie na oferty miast i wybor miasta ----
        if (aktualna_faza_ubiegania_sie == CZEKAM_NA_OFERTY) {
            if (MINAL_TIMEOUT_NA_OFERTY() || OTRZYMANO_WYSTARCZAJACO_OFERT()) {
                zegar_lamporta++;
                WYPISZ_LOG(moj_rank_mpi, zegar_lamporta, "Analizuję otrzymane oferty.");
                wybrane_miasto_do_REQ = -1;
                int najlepszy_zegar_dostepnosci = NIESKONCZONOSC;

                for (int i = 0; i < liczba_otrzymanych_ofert; ++i) {
                    OTRZYMANA_OFERTA oferta = otrzymane_oferty_miast[i];
                    if (status_miasta[oferta.id_oferowanego_miasta] != ODPOCZYWA || oferta.zegar_dostepnosci_miasta_z_oferty < miasto_odpoczywa_do_zegara[oferta.id_oferowanego_miasta]) {
                        if (oferta.zegar_dostepnosci_miasta_z_oferty < najlepszy_zegar_dostepnosci) {
                            najlepszy_zegar_dostepnosci = oferta.zegar_dostepnosci_miasta_z_oferty;
                            wybrane_miasto_do_REQ = oferta.id_oferowanego_miasta;
                        } else if (oferta.zegar_dostepnosci_miasta_z_oferty == najlepszy_zegar_dostepnosci && oferta.id_oferowanego_miasta < wybrane_miasto_do_REQ) {
                            wybrane_miasto_do_REQ = oferta.id_oferowanego_miasta;
                        }
                    }
                }

                if (wybrane_miasto_do_REQ != -1) {
                    zegar_lamporta++;
                    zegar_mojego_REQ_o_konkretne_miasto = zegar_lamporta;
                    licznik_otrzymanych_ACK_dla_konkretnego_miasta = 0;
                    WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Wybrałem miasto %d. Wysyłam REQ_SPECIFIC_CITY.", wybrane_miasto_do_REQ);
                    ROZESLIJ_DO_WSZYSTKICH_INNYCH(REQ_SPECIFIC_CITY, moj_rank_mpi, zegar_mojego_REQ_o_konkretne_miasto, wybrane_miasto_do_REQ);
                    status_miasta[wybrane_miasto_do_REQ] = OCZEKUJE_NA_ACK_DLA_KONKRETNEGO;
                    aktualna_faza_ubiegania_sie = WYBRALEM_OFERTE_CZEKAM_NA_ACK;
                } else {
                    WYPISZ_LOG(moj_rank_mpi, zegar_lamporta, "Brak odpowiednich ofert. Spróbuję później.");
                    aktualna_faza_ubiegania_sie = IDLE;
                    CZEKAJ(rand() % 500 + 100);
                }
            }
        }

        // ---- faza 3: ubieganie się o konkretne miasto i wejście ----
        if (aktualna_faza_ubiegania_sie == WYBRALEM_OFERTE_CZEKAM_NA_ACK) {
            if (licznik_otrzymanych_ACK_dla_konkretnego_miasta == (N_ZABOJCOW - 1)) {
                zegar_lamporta++;
                WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Otrzymałem wszystkie ACK. Wchodzę do miasta %d.", wybrane_miasto_do_REQ);
                status_miasta[wybrane_miasto_do_REQ] = W_MIESCIE;
                aktualna_faza_ubiegania_sie = W_MIESCIE_P_i;

                CZEKAJ(rand() % 2000 + 1000); // symulacja pobytu

                // opuszczanie miasta
                zegar_lamporta++;
                WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Opuszczam miasto %d.", wybrane_miasto_do_REQ);
                miasto_odpoczywa_do_zegara[wybrane_miasto_do_REQ] = zegar_lamporta + CZAS_ODPOCZYNKU_MIASTA_LOGICZNY;
                ROZESLIJ_DO_WSZYSTKICH_INNYCH(RELEASE_CITY, moj_rank_mpi, zegar_lamporta, wybrane_miasto_do_REQ, miasto_odpoczywa_do_zegara[wybrane_miasto_do_REQ]);
                status_miasta[wybrane_miasto_do_REQ] = ODPOCZYWA;

                ObsluzOdlozoneZadaniaDlaMiasta(wybrane_miasto_do_REQ);

                aktualna_faza_ubiegania_sie = IDLE;
                wybrane_miasto_do_REQ = -1;
            }
        }

        // ---- obsługa przychodzących komunikatów (MPI_Irecv + MPI_Test) ----
        KOMUNIKAT msg = ODBIERZ_KOMUNIKAT_NIEBLOKUJACY();
        if (msg.typ != (TYP_KOMUNIKATU)(-1)) {
            AktualizujZegar(msg.zegar_nadawcy_oryginalny);
            WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Odebrałem komunikat typu %d od %d z zegarem %d", 
                                msg.typ, msg.id_nadawcy, msg.zegar_nadawcy_oryginalny);

            switch (msg.typ) {
                case WANT_ANY_CITY_REQ: {
                    zegar_lamporta++;
                    // wybierz najlepsze miasto do zaoferowania
                    int id_oferowanego_miasta_przez_mnie = -1;
                    int najlepszy_lokalny_zegar_dostepnosci = NIESKONCZONOSC;
                    int najlepsza_odleglosc = M_MIAST + 1;

                    for (int m = 0; m < M_MIAST; ++m) {
                        int aktualny_zegar_dostep_m = (status_miasta[m] == ODPOCZYWA) ? miasto_odpoczywa_do_zegara[m] : zegar_lamporta;
                        // nie oferuj miasta, o które sam intensywnie walczę lub które jest długo niedostępne
                        if ((status_miasta[m] == WOLNE || (status_miasta[m] == ODPOCZYWA && aktualny_zegar_dostep_m < NIESKONCZONOSC)) &&
                            (status_miasta[m] != OCZEKUJE_NA_ACK_DLA_KONKRETNEGO || m != wybrane_miasto_do_REQ) &&
                            (status_miasta[m] != W_MIESCIE || m != wybrane_miasto_do_REQ)) {

                            int odleglosc = abs(m - msg.id_nadawcy);

                            if (aktualny_zegar_dostep_m < najlepszy_lokalny_zegar_dostepnosci ||
                                (aktualny_zegar_dostep_m == najlepszy_lokalny_zegar_dostepnosci &&
                                    odleglosc < najlepsza_odleglosc) ||
                                (aktualny_zegar_dostep_m == najlepszy_lokalny_zegar_dostepnosci &&
                                    odleglosc == najlepsza_odleglosc &&
                                    m < id_oferowanego_miasta_przez_mnie)) {
                                
                                    najlepszy_lokalny_zegar_dostepnosci = aktualny_zegar_dostep_m;
                                    id_oferowanego_miasta_przez_mnie = m;
                                    najlepsza_odleglosc = odleglosc;
                            }
                        }
                    }

                    if (id_oferowanego_miasta_przez_mnie != -1) {
                        WYSLIJ_DO(msg.id_nadawcy, OFFER_CITY_RESP, moj_rank_mpi, zegar_lamporta,
                                id_oferowanego_miasta_przez_mnie, najlepszy_lokalny_zegar_dostepnosci);
                        WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Oferuję miasto %d dla procesu %d", 
                                           id_oferowanego_miasta_przez_mnie, msg.id_nadawcy);
                    } else {
                        WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Nie mam dobrej oferty dla %d", msg.id_nadawcy);
                    }
                    break;
                }
                case OFFER_CITY_RESP: {
                    if (aktualna_faza_ubiegania_sie == CZEKAM_NA_OFERTY) {
                        DODAJ_DO_LISTY_OFERT(msg.payload.oferta); // payload zawiera id_oferujacego, zegar_oferujacego, id_ofer_miasta, zegar_dostep_miasta
                        WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Otrzymałem ofertę miasta %d od %d", 
                                           msg.payload.oferta.id_oferowanego_miasta, msg.payload.oferta.id_oferujacego);
                    }
                    break;
                }
                case REQ_SPECIFIC_CITY: {
                    int id_zad_miasta = msg.payload.id_miasta_req;
                    int zegar_zadajacego_oryginalny = msg.zegar_nadawcy_oryginalny;
                    int czy_odroczyc = 0;

                    // PIERWSZEŃSTWO: Jeśli ja aktywnie ubiegam się o to miasto
                    if (aktualna_faza_ubiegania_sie == WYBRALEM_OFERTE_CZEKAM_NA_ACK &&
                        wybrane_miasto_do_REQ == id_zad_miasta) {
                        if (zegar_mojego_REQ_o_konkretne_miasto < zegar_zadajacego_oryginalny ||
                            (zegar_mojego_REQ_o_konkretne_miasto == zegar_zadajacego_oryginalny && moj_rank_mpi < msg.id_nadawcy)) {
                            // Ja mam priorytet
                            czy_odroczyc = 1;
                        } else {
                            // Przychodzące żądanie ma priorytet (lub równe i niższy rank)
                            // Wysyłam ACK, ja prawdopodobnie stracę to miasto (muszę to jakoś obsłużyć, np. wrócić do IDLE?)
                            // Na razie po prostu nie odraczam.
                            czy_odroczyc = 0;
                        }
                    } else if (status_miasta[id_zad_miasta] == W_MIESCIE && wybrane_miasto_do_REQ == id_zad_miasta) {
                        // Ja jestem w tym mieście (choć nie powinienem być jeśli faza nie jest W_MIESCIE_P_i)
                        // Ale jeśli jestem, to odraczam.
                        czy_odroczyc = 1;
                    } else if (status_miasta[id_zad_miasta] == ODPOCZYWA && zegar_lamporta < miasto_odpoczywa_do_zegara[id_zad_miasta]) {
                        // Miasto odpoczywa i jeszcze nie jest dostępne dla nikogo
                        czy_odroczyc = 1;
                    }
                    // Jeśli żaden z powyższych warunków na odroczenie nie jest spełniony,
                    // LUB jeśli ja nie ubiegam się o to miasto i nie jest ono u mnie zajęte/odpoczywające,
                    // to wysyłamy ACK.

                    if (czy_odroczyc) {
                        DODAJ_DO_LISTY_ODLOZONEJ_REQ(id_zad_miasta, (ODLOZONE_REQ){msg.id_nadawcy, zegar_zadajacego_oryginalny});
                        WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Odkładam REQ od %d o miasto %d", msg.id_nadawcy, id_zad_miasta);
                    } else {
                        zegar_lamporta++;
                        WYSLIJ_DO(msg.id_nadawcy, ACK_SPECIFIC_CITY, moj_rank_mpi, zegar_lamporta, id_zad_miasta);
                        WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Wysyłam ACK do %d dla miasta %d", msg.id_nadawcy, id_zad_miasta);
                    }
                    break;
                }
                case ACK_SPECIFIC_CITY: {
                    if (aktualna_faza_ubiegania_sie == WYBRALEM_OFERTE_CZEKAM_NA_ACK && msg.payload.id_miasta_ack == wybrane_miasto_do_REQ) {
                        licznik_otrzymanych_ACK_dla_konkretnego_miasta++;
                        WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Otrzymałem ACK od %d dla miasta %d (%d/%d)", 
                                           msg.id_nadawcy, msg.payload.id_miasta_ack, 
                                           licznik_otrzymanych_ACK_dla_konkretnego_miasta, N_ZABOJCOW - 1);
                    }
                    break;
                }
                case RELEASE_CITY: {
                    int id_zwol_miasta = msg.payload.release_info.id_zwolnionego_miasta;
                    int zegar_kiedy_koniec_odpoczynku = msg.payload.release_info.zegar_do_kiedy_odpoczywa;
                    status_miasta[id_zwol_miasta] = ODPOCZYWA;
                    miasto_odpoczywa_do_zegara[id_zwol_miasta] = zegar_kiedy_koniec_odpoczynku;
                    WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Miasto %d zwolnione przez %d, odpoczywa do %d", 
                                       id_zwol_miasta, msg.id_nadawcy, zegar_kiedy_koniec_odpoczynku);
                    ObsluzOdlozoneZadaniaDlaMiasta(id_zwol_miasta); // sprawdź, czy można komuś wysłać ACK
                    break;
                }
            }
        }

        // ---- sprawdź, czy miasta przestały odpoczywać i obsłuż odłożone żądania 
        for (int m = 0; m < M_MIAST; ++m) {
            if (status_miasta[m] == ODPOCZYWA && zegar_lamporta >= miasto_odpoczywa_do_zegara[m]) {
                status_miasta[m] = WOLNE;
                WYPISZ_LOG_FORMATTED(moj_rank_mpi, zegar_lamporta, "Miasto %d zakończyło odpoczynek.", m);
                ObsluzOdlozoneZadaniaDlaMiasta(m);
            }
        }
    }
}