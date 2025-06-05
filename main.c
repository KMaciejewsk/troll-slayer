#include <mpi.h>
#include "troll_slayer.h"
#include <stdio.h>

int N_ZABOJCOW;
int M_MIAST;
int CZAS_ODPOCZYNKU_MIASTA_LOGICZNY = 5;

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int moj_rank_mpi;
    MPI_Comm_rank(MPI_COMM_WORLD, &moj_rank_mpi);
    MPI_Comm_size(MPI_COMM_WORLD, &N_ZABOJCOW);

    if (argc < 2) {
        M_MIAST = 10;
        if (moj_rank_mpi == 0) {
            printf("Brak argumentu liczby miast. Ustawiam domyślną liczbę miast na %d.\n", M_MIAST);
        }
    } else {
        M_MIAST = atoi(argv[1]);

        if (M_MIAST <= 0 || M_MIAST > MAX_MIAST) {
            if (moj_rank_mpi == 0) {
                fprintf(stderr, "Błąd: Liczba miast musi być dodatnia i nie większa niż %d. Zakończenie programu.\n", MAX_MIAST);
            }
            MPI_Finalize();
            return 1;
        }
    }

    printf("Proces %d wystartował z %d miastami.\n", moj_rank_mpi, M_MIAST);

    ZycieZabojcyMPI(moj_rank_mpi);

    MPI_Finalize();
    return 0;
}