#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "mpi.h"
#include "config.h"
#include <assert.h>
#include <unistd.h>
#include <math.h>

#include "debug.h"
#include "mpifunc.h"

int mpiBlastnSplitBase() {

	DEBUG_ENTER_FUNCTION;

	FILE *fp;
	FILE *fp_array[256]; /* Number of nodes */

	int i;
	int index = 0;

	char sequence[1000];
	char file_name[20];

	fp = fopen(globals.queries, "r");
	assert(fp);

	while (!feof(fp)) {
		fscanf(fp, "%s", sequence);
		if (sequence[0] == '>')
			blastconf.sequence_number++;
	}

	blastconf.sequence_proc_total = floor(blastconf.sequence_number
			/ globals.total_proc);

	/* Coloca o ponteiro no inicio do arquivo novamente */
	rewind(fp);

	/* Open splited files, to write the sequences */
	for (i = 0; i < globals.total_proc; i++) {
		memset(file_name, 0, sizeof(file_name));
		sprintf(file_name, "tmp_%d.fas", i);
		fp_array[i] = fopen(file_name, "w");
		assert(fp_array[i]);
	}

	/* Split entries */
	while (!feof(fp)) {

		fgets(sequence, sizeof(sequence), fp);

		if (sequence[0] == '>') {
			if (index == (globals.total_proc))
				index = 1;
			else
				index++;
		} 

		fputs(sequence, fp_array[index-1]);
		memset(sequence, 0, sizeof(sequence));

	}

	/* Close splited files, to write the sequences */
	for (i = 0; i < globals.total_proc; i++) {
		if (fp_array[i])
			fclose(fp_array[i]);
	}

	fclose(fp);

	DEBUG_EXIT_FUNCTION;

	return 0;
}

void mpiBlastnExecute() {

	FILE *fp, *fp2;
	int i;

	char sequence[1000];
	char file_name[20];
	char cmd[200];

	char out[200];

	int buffer;

	memset(sequence, 0, sizeof(sequence));
	memset(file_name, 0, sizeof(file_name));
	memset(cmd, 0, sizeof(cmd));

	/*Split entries*/
	if (globals.my_rank == 0) {

		DEBUG_MSG("Splitting sequences ...\n");

		/* Split entries */
		mpiBlastnSplitBase();

		DEBUG_MSG("done.\n");

		sleep(2);
	}

	/* Start processing negotiation */
	if (globals.my_rank == 0) {

		buffer = start;

		/* Sequences already splitted */
		for (i = 1; i < globals.total_proc; i++) {
			MPI_Send(&buffer, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
		}

	} else {
		/* Slave is waiting for split sequences*/
		MPI_Recv(&buffer, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
	}

	DEBUG_MSG("Start computation\n");

	/* Start mpi time */
	mpiStartTime();

	/*Computacao sobre os dados*/
	memset(file_name, 0, sizeof(file_name));
	memset(out, 0, sizeof(out));
	memset(cmd, 0, sizeof(cmd));

	sprintf(file_name, "tmp_%d.fas", globals.my_rank);
	sprintf(out, "out.%d.fasta", globals.my_rank);

	sprintf(
			cmd,
			"./blastall -p blastp -query %s -db %s -out %s -outfmt 6 -num_descriptions 1 -num_alignments 1 -num_threads %d",
			file_name, globals.base_name, out, globals.num_threads);

	DEBUG_MSG("%s\n", cmd);
	system(cmd);

	unlink(file_name);

	/* Waiting for all slave process */
	if (globals.my_rank == 0) {

		for (i = 1; i < globals.total_proc; i++)
			MPI_Recv(&buffer, 1, MPI_INT, i, MPI_ANY_TAG, MPI_COMM_WORLD,
					&status);

		DEBUG_MSG("End of master computation\n");

	} else {

		buffer = stop;

		/* Send finalization signal to master */
		MPI_Send(&buffer, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);

		DEBUG_MSG("End computation\n");
	}

	/* Master concat the results */
	if (globals.my_rank == 0) {

		fp = fopen(globals.out, "w");
		for (i = 0; i < globals.total_proc; i++) {
			sprintf(out, "out.%d.fasta", i);

			fp2 = fopen(out, "r");
			while (!feof(fp2)) {

				fgets(sequence, sizeof(sequence), fp2);
				fputs(sequence, fp);
				memset(sequence, 0, sizeof(sequence));

			}

			fclose(fp2);
			unlink(out);

			memset(out, 0, sizeof(out));

		}
		fclose(fp);

		mpiEndTime();

		DEBUG_MSG("Master - Execution Time: %.1f seconds\n", mpiGetExectime());
	}
}
