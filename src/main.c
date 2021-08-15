#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#define SECTOR_SIZE 512
#define CLUSTER_SIZE (2 * SECTOR_SIZE)
#define ENTRY_BY_CLUSTER (CLUSTER_SIZE / sizeof(dir_entry_t))
#define NUM_CLUSTER 4096
#define FAT_NAME "fat.part"
#define END_FILE 0xffff
#define CLUSTER_START (CLUSTER_SIZE) * 10

#define IS_FILE 0
#define IS_DIR 1
#define CLUSTER_FREE 0
#define CLUSTER_OCCUPIED 1

typedef struct
{
    uint8_t filename[18];
    uint8_t attributes;
    uint8_t reserved[7];
    uint16_t first_block;
    uint32_t size;
} dir_entry_t;

typedef union
{
    dir_entry_t dir[CLUSTER_SIZE / sizeof(dir_entry_t)];
    uint8_t data[CLUSTER_SIZE];
} data_cluster;

uint8_t boot_block[CLUSTER_SIZE];
uint16_t fat[NUM_CLUSTER];
dir_entry_t root_dir[ENTRY_BY_CLUSTER];
data_cluster clusters[4086];
uint8_t free_clusters[NUM_CLUSTER];

int find_free_cluster()
{
    for (int cluster_entry = 9; cluster_entry < NUM_CLUSTER; cluster_entry++)
    {
        if (free_clusters[cluster_entry] == CLUSTER_FREE)
        {
            free_clusters[cluster_entry] = CLUSTER_OCCUPIED;

            return cluster_entry;
        }
    }

    return -1;
}

void write_data(int cluster, data_cluster data)
{
    FILE *file = fopen(FAT_NAME, "rb+");
    fseek(file, (cluster * CLUSTER_SIZE), SEEK_SET);
    fwrite(&data, sizeof(data), 1, file);
    fclose(file);
}

data_cluster load_data(int cluster)
{
    data_cluster data;
    if (cluster < 9)
    {
        printf("Cluster inválido\n");
        return data;
    }
    else if (cluster == 9)
    {
        memcpy(&data, root_dir, sizeof(root_dir));
        return data;
    }

    FILE *file = fopen(FAT_NAME, "rb+");
    fseek(file, (cluster * CLUSTER_SIZE), SEEK_SET);
    fread(&data, sizeof(data), 1, file);
    fclose(file);
    return data;
}

void write_fat()
{
    FILE *file = fopen(FAT_NAME, "rb+");
    fseek(file, CLUSTER_SIZE, SEEK_SET);
    fwrite(&fat, sizeof(fat), 1, file);
    fclose(file);
}

void init()
{
    char response;
    printf("Todos os seus arquivos serão excluídos no processo, deseja continuar? [s/N] ");

    setbuf(stdin, NULL);
    response = getc(stdin);
    if (response != 's' && response != 'S')
        return;

    FILE *file;
    file = fopen(FAT_NAME, "wb");
    if (file == NULL)
    {
        printf("Erro ao abrir o arquivo\n");
        exit(1);
    }
    //Preenche o boot block com o padrão 0xbb, e o escreve no arquivo
    for (int i = 0; i < CLUSTER_SIZE; i++)
    {
        boot_block[i] = 0xbb;
    }
    fwrite(&boot_block, sizeof(boot_block), 1, file);

    //Preenche a fat
    fat[0] = 0xfffd;
    for (int i = 1; i < 9; i++)
        fat[i] = 0xfffe;
    fat[9] = END_FILE;
    for (int i = 10; i < NUM_CLUSTER; i++)
        fat[i] = 0x0000;
    fwrite(&fat, sizeof(fat), 1, file);

    //Preenche o root_dir com o padrão 0x00, e o escreve no arquivo
    memset(root_dir, 0x00, sizeof(root_dir));
    fwrite(&root_dir, sizeof(root_dir), 1, file);

    //Preenche os clusters com o padrão 0x00, e o escreve no arquivo
    memset(clusters, 0x00, sizeof(clusters));
    fwrite(&clusters, sizeof(clusters), 1, file);

    fclose(file);
    setbuf(stdin, NULL);
    getc(stdin);

    printf("Operação concluída!\n");
}

//Carrega a tabela fat da memoria
void load(int flag)
{
    FILE *file;
    file = fopen(FAT_NAME, "rb");
    if (file == NULL)
    {
        printf("Erro ao abrir o arquivo\n");
        exit(1);
    }

    fread(&boot_block, sizeof(boot_block), 1, file);
    fread(&fat, sizeof(fat), 1, file);
    fread(&root_dir, sizeof(root_dir), 1, file);
    fclose(file);
    for (int i = 0; i < NUM_CLUSTER; i++)
    {
        free_clusters[i] = fat[i] == CLUSTER_FREE ? CLUSTER_FREE : CLUSTER_OCCUPIED;
    }
    if (flag)
        printf("Operação concluída!\n");
}

/*Cria uma nova entrada:
    - attributes = 1 →
*/
void new_entry(char dir[18], data_cluster parent_dir, int parent_cluster, int attributes)
{
    int cluster_entry = find_free_cluster();

    int dir_entry, flag = 0;
    for (dir_entry = 0; dir_entry < ENTRY_BY_CLUSTER; dir_entry++)
    {
        if (strcmp(dir, parent_dir.dir[dir_entry].filename) == 0)
        {
            flag = 1;
            break;
        }
        else if (parent_dir.dir[dir_entry].size == 0)
            break;
    }

    if (flag)
    {
        printf("O nome \"%s\" já está em uso\n", dir);
        return;
    }

    if (dir_entry == ENTRY_BY_CLUSTER)
    {
        printf("Impossível criar o novo diretório\nDiretório pai está cheio!\n");
        return;
    }

    if (cluster_entry == -1)
    {
        printf("Impossível criar o novo diretório\nO disco está cheio!\n");
        return;
    }

    fat[cluster_entry] = END_FILE;

    dir_entry_t entry;
    strcpy(entry.filename, dir);
    entry.attributes = attributes;
    entry.first_block = cluster_entry;
    entry.size = CLUSTER_SIZE;

    parent_dir.dir[dir_entry] = entry;

    data_cluster new_entry;
    memset(&new_entry, 0x00, sizeof(new_entry));

    // atualiza a pasta pai
    write_data(parent_cluster, parent_dir);

    // limpa a memória para a nova entrada
    write_data(cluster_entry, new_entry);

    // atualiza o fat no disco
    write_fat();

    if (attributes)
        printf("Diretório \"%s\" criado!\n", dir);
    else
        printf("Arquivo \"%s\" criado!\n", dir);
}

void ls(data_cluster parent_dir)
{
    int i;
    for (i = 0; i < ENTRY_BY_CLUSTER; i++)
    {
        if (parent_dir.dir[i].size == 0)
            break;
        printf(parent_dir.dir[i].attributes == IS_DIR ? "D - " : "A - ");
        printf("%s - %dB\n", parent_dir.dir[i].filename, parent_dir.dir[i].size);
    }
    if (i == 0)
    {
        printf("Diretório vazio\n");
    }
}

void del(char dir[18], data_cluster parent_dir, int parent_cluster)
{
    int i;
    for (i = 0; i < ENTRY_BY_CLUSTER; i++)
    {
        if (strcmp(dir, parent_dir.dir[i].filename) == 0)
        {
            break;
        }
    }
    if (i == ENTRY_BY_CLUSTER)
    {
        printf("O arquivo ou diretório \"%s\" não existe", dir);
        return;
    }

    if (parent_dir.dir[i].attributes == IS_FILE)
    {
        int block = parent_dir.dir[i].first_block, aux;
        while (block != END_FILE)
        {
            aux = block;
            block = fat[block];
            fat[aux] = 0x00;
        }
        fat[block] = 0x00;
        printf("Arquivo delatado com sucesso!\n");
    }

    data_cluster data = load_data(parent_dir.dir[i].first_block);

    if (data.dir[0].size != 0)
    {
        printf("O diretório \"%s\" não está vazio\n", parent_dir.dir[i].filename);
        return;
    }
    fat[parent_dir.dir[i].first_block] = 0x00;
    memset(&(parent_dir.dir[i]), 0x00, sizeof(parent_dir.dir[i]));
    write_fat();
    write_data(parent_cluster, parent_dir);
    printf("Diretório delatado com sucesso!\n");
}

int write_file(char *stream, int first_cluster)
{
    printf("steam / cluster size: %f\n", (float)strlen(stream) / CLUSTER_SIZE);
    long num_blocks = ceil((float)strlen(stream) / CLUSTER_SIZE);
    printf("Blocos necessários: %d\n", num_blocks);
    int curr_cluster = first_cluster;

    while (fat[curr_cluster] != END_FILE)
    {
        int aux = curr_cluster;
        curr_cluster = fat[aux];
        fat[aux] = 0x00;
    }
    fat[curr_cluster] = 0x00;

    fat[first_cluster] = END_FILE;
    curr_cluster = first_cluster;
    for (int i = 1; i < num_blocks; i++)
    {
        int free_cluster = find_free_cluster();
        if (free_cluster == -1)
        {
            fat[curr_cluster] = END_FILE;
            printf("O disco está cheio!\n");

            int curr_cluster = first_cluster;
            while (fat[curr_cluster] != END_FILE)
            {
                int aux = curr_cluster;
                curr_cluster = fat[aux];
                fat[aux] = 0x00;
            }
            fat[curr_cluster] = 0x00;
            fat[first_cluster] = END_FILE;

            return CLUSTER_SIZE;
        }
        fat[curr_cluster] = free_cluster;
        curr_cluster = fat[curr_cluster];
    }
    fat[curr_cluster] = END_FILE;

    curr_cluster = first_cluster;
    for (int i = 0; i < num_blocks; i++)
    {
        data_cluster data;
        memset(&data, 0x00, sizeof(data));

        if (i + 1 == num_blocks)
            memcpy(&data, stream, strlen(stream));
        else
            memcpy(&data, stream, CLUSTER_SIZE);

        write_data(curr_cluster, data);
        stream += 1024;
        curr_cluster = fat[curr_cluster];
    }
    write_fat();
    return num_blocks * CLUSTER_SIZE;
}

char *read_file(int first_cluster, int size)
{
    int curr_cluster = first_cluster, num_blocks = size / CLUSTER_SIZE;

    data_cluster file_data[num_blocks];
    char *stream = (char *)file_data;

    for (int i = 0; i < num_blocks; i++)
    {
        file_data[i] = load_data(curr_cluster);
        curr_cluster = fat[curr_cluster];
    }

    return stream;
}

int append_file(char *stream, int first_cluster, int curr_size)
{
    int curr_cluster = first_cluster, num_blocks = curr_size / CLUSTER_SIZE, final_cluster;
    data_cluster file_data;
    char *file_stream;
    int new_blocks = 0;

    while (curr_cluster != END_FILE)
    {
        final_cluster = curr_cluster;
        curr_cluster = fat[curr_cluster];
    }

    file_stream = read_file(final_cluster, CLUSTER_SIZE);

    int len_final_cluster = strlen(file_stream);
    if (len_final_cluster == CLUSTER_SIZE)
        new_blocks = ceil((float)strlen(stream) / CLUSTER_SIZE);
    else
        new_blocks = ceil((float)(strlen(stream) + len_final_cluster) / CLUSTER_SIZE) - 1;

    for (int i = 0; i < new_blocks; i++)
    {
        int free_cluster = find_free_cluster();
        if (free_cluster == -1)
        {
            fat[curr_cluster] = END_FILE;
            printf("O disco está cheio!\n");

            int curr_cluster = final_cluster;
            while (fat[curr_cluster] != END_FILE)
            {
                int aux = curr_cluster;
                curr_cluster = fat[aux];
                fat[aux] = 0x00;
                free_clusters[aux] = CLUSTER_FREE;
            }
            fat[curr_cluster] = 0x00;
            free_clusters[curr_cluster] = CLUSTER_FREE;
            fat[final_cluster] = END_FILE;

            return CLUSTER_SIZE;
        }
        fat[curr_cluster] = free_cluster;
        curr_cluster = fat[curr_cluster];
    }
    fat[curr_cluster] = END_FILE;

    if (len_final_cluster < CLUSTER_SIZE)
    {
        FILE *file = fopen(FAT_NAME, "rb+");
        fseek(file, final_cluster * CLUSTER_SIZE + len_final_cluster, SEEK_SET);

        if (strlen(stream) >= CLUSTER_SIZE - len_final_cluster)
        {
            fwrite(stream, CLUSTER_SIZE - len_final_cluster, 1, file);
            stream += CLUSTER_SIZE - len_final_cluster;
        }
        else
        {
            fwrite(stream, strlen(stream), 1, file);
            fclose(file);
            return curr_size;
        }
        
        fclose(file);
    }

    curr_cluster = fat[final_cluster];
    for (int i = 0; i < new_blocks; i++)
    {
        data_cluster data;
        memset(&data, 0x00, sizeof(data));

        if (i + 1 == new_blocks)
            memcpy(&data, stream, strlen(stream));
        else
            memcpy(&data, stream, CLUSTER_SIZE);

        write_data(curr_cluster, data);
        stream += 1024;
        curr_cluster = fat[curr_cluster];
    }

    return curr_size + (new_blocks * CLUSTER_SIZE);
}

int main()
{
    char *input;
    while ((input = readline("SHELL V-POWER → ")) != 0)
    {
        load(0);

        add_history(input);
        char *command = strtok(input, " ");

        if (strcmp(command, "init") == 0)
        {
            init();
        }
        else if (strcmp(command, "load") == 0)
        {
            load(1);
        }
        else if (strcmp(command, "mkdir") == 0)
        {
            char *next = NULL;
            char *dir = strtok(NULL, "/");
            int parent_cluster = 9;

            while (1)
            {
                next = strtok(NULL, "/");
                data_cluster parent_dir = load_data(parent_cluster);
                // mkdir /teste/foo
                if (next == NULL)
                {
                    if (dir == NULL)
                    {
                        printf("Não é possível criar a pasta raiz\n");
                        break;
                    }

                    new_entry(dir, parent_dir, parent_cluster, IS_DIR);
                    break;
                }

                int i;
                for (i = 0; i < ENTRY_BY_CLUSTER; i++)
                {
                    if (strcmp(dir, parent_dir.dir[i].filename) == 0 && parent_dir.dir[i].attributes == IS_DIR)
                    {
                        parent_cluster = parent_dir.dir[i].first_block;
                        break;
                    }
                }

                if (i == ENTRY_BY_CLUSTER)
                {
                    printf("O diretorio \"%s\" não existe\n", dir);
                    break;
                }

                dir = next;
            }
        }
        else if (strcmp(command, "ls") == 0)
        {
            char *dir = NULL;
            int parent_cluster = 9;

            while (1)
            {
                data_cluster parent_dir = load_data(parent_cluster);

                if ((dir = strtok(NULL, "/")) == NULL)
                {
                    ls(parent_dir);
                    break;
                }

                int i;
                for (i = 0; i < ENTRY_BY_CLUSTER; i++)
                {
                    if (strcmp(dir, parent_dir.dir[i].filename) == 0 && parent_dir.dir[i].attributes == IS_DIR)
                    {
                        parent_cluster = parent_dir.dir[i].first_block;
                        break;
                    }
                }

                if (i == ENTRY_BY_CLUSTER)
                {
                    printf("O diretorio \"%s\" não existe\n", dir);
                    break;
                }
            }
        }
        else if (strcmp(command, "create") == 0)
        {
            char *next = NULL;
            char *dir = strtok(NULL, "/");
            int parent_cluster = 9;

            while (1)
            {
                next = strtok(NULL, "/");
                data_cluster parent_dir = load_data(parent_cluster);
                // mkdir /teste/foo
                if (next == NULL)
                {
                    if (dir == NULL)
                    {
                        printf("Nome inválido\n");
                        break;
                    }

                    new_entry(dir, parent_dir, parent_cluster, IS_FILE);
                    break;
                }

                int i;
                for (i = 0; i < ENTRY_BY_CLUSTER; i++)
                {
                    if (strcmp(dir, parent_dir.dir[i].filename) == 0 && parent_dir.dir[i].attributes == IS_DIR)
                    {
                        parent_cluster = parent_dir.dir[i].first_block;
                        break;
                    }
                }

                if (i == ENTRY_BY_CLUSTER)
                {
                    printf("O diretorio \"%s\" não existe\n", dir);
                    break;
                }

                dir = next;
            }
        }
        else if (strcmp(command, "unlink") == 0)
        {
            char *next = NULL;
            char *dir = strtok(NULL, "/");
            int parent_cluster = 9;

            while (1)
            {
                next = strtok(NULL, "/");
                data_cluster parent_dir = load_data(parent_cluster);
                // mkdir /teste/foo
                if (next == NULL)
                {
                    if (dir == NULL)
                    {
                        printf("Nome inválido\n");
                        break;
                    }

                    del(dir, parent_dir, parent_cluster);
                    break;
                }

                int i;
                for (i = 0; i < ENTRY_BY_CLUSTER; i++)
                {
                    if (strcmp(dir, parent_dir.dir[i].filename) == 0 && parent_dir.dir[i].attributes == IS_DIR)
                    {
                        parent_cluster = parent_dir.dir[i].first_block;
                        break;
                    }
                }

                if (i == ENTRY_BY_CLUSTER)
                {
                    printf("O diretorio \"%s\" não existe\n", dir);
                    break;
                }

                dir = next;
            }
        }
        else if (strcmp(command, "write") == 0)
        {
            char *stream = strtok(NULL, "\"");
            char *path = strtok(NULL, " ");
            char *next = NULL;
            char *file = strtok(path, "/");
            int parent_cluster = 9;

            while (1)
            {
                next = strtok(NULL, "/");
                data_cluster parent_dir = load_data(parent_cluster);
                // mkdir /teste/foo
                if (next == NULL)
                {
                    if (file == NULL)
                    {
                        printf("Nome inválido\n");
                        break;
                    }

                    for (int i = 0; i < ENTRY_BY_CLUSTER; i++)
                    {
                        if (strcmp(parent_dir.dir[i].filename, file) == 0)
                        {
                            parent_dir.dir[i].size = write_file(stream, parent_dir.dir[i].first_block);
                            write_data(parent_cluster, parent_dir);
                        }
                    }

                    //new_entry(file, parent_dir, parent_cluster, IS_FILE);
                    break;
                }

                int i;
                for (i = 0; i < ENTRY_BY_CLUSTER; i++)
                {
                    if (strcmp(file, parent_dir.dir[i].filename) == 0 && parent_dir.dir[i].attributes == IS_DIR)
                    {
                        parent_cluster = parent_dir.dir[i].first_block;
                        break;
                    }
                }

                if (i == ENTRY_BY_CLUSTER)
                {
                    printf("O diretorio \"%s\" não existe\n", file);
                    break;
                }

                file = next;
            }
        }
        else if (strcmp(command, "read") == 0)
        {
            char *next = NULL;
            char *entry = strtok(NULL, "/");
            int curr_cluster = 9;
            uint32_t size;

            while (1)
            {
                data_cluster parent_dir = load_data(curr_cluster);
                if ((next = strtok(NULL, "/")) == NULL)
                {
                    if (entry == NULL)
                    {
                        printf("Entrada inválida!\n");
                        break;
                    }

                    int i;
                    for (i = 0; i < ENTRY_BY_CLUSTER; i++)
                    {
                        if (strcmp(entry, parent_dir.dir[i].filename) == 0 && parent_dir.dir[i].attributes == IS_FILE)
                        {
                            curr_cluster = parent_dir.dir[i].first_block;
                            size = parent_dir.dir[i].size;
                            break;
                        }
                    }

                    if (i == ENTRY_BY_CLUSTER)
                    {
                        printf("Entrada inválida!\n");
                        break;
                    }

                    printf("%s\n", read_file(curr_cluster, size));
                    break;
                }

                int i;
                for (i = 0; i < ENTRY_BY_CLUSTER; i++)
                {
                    if (strcmp(entry, parent_dir.dir[i].filename) == 0 && parent_dir.dir[i].attributes == IS_DIR)
                    {
                        curr_cluster = parent_dir.dir[i].first_block;
                        break;
                    }
                }

                if (i == ENTRY_BY_CLUSTER)
                {
                    printf("O diretorio \"%s\" não existe\n", entry);
                    break;
                }
            }
        }
        else if (strcmp(command, "append") == 0)
        {
            char *stream = strtok(NULL, "\"");
            char *path = strtok(NULL, " ");
            char *next = NULL;
            char *file = strtok(path, "/");
            int parent_cluster = 9;

            while (1)
            {
                next = strtok(NULL, "/");
                data_cluster parent_dir = load_data(parent_cluster);
                // mkdir /teste/foo
                if (next == NULL)
                {
                    if (file == NULL)
                    {
                        printf("Nome inválido\n");
                        break;
                    }

                    for (int i = 0; i < ENTRY_BY_CLUSTER; i++)
                    {
                        if (strcmp(parent_dir.dir[i].filename, file) == 0)
                        {
                            parent_dir.dir[i].size = append_file(stream, parent_dir.dir[i].first_block, parent_dir.dir[i].size);
                            write_data(parent_cluster, parent_dir);
                        }
                    }

                    break;
                }

                int i;
                for (i = 0; i < ENTRY_BY_CLUSTER; i++)
                {
                    if (strcmp(file, parent_dir.dir[i].filename) == 0 && parent_dir.dir[i].attributes == IS_DIR)
                    {
                        parent_cluster = parent_dir.dir[i].first_block;
                        break;
                    }
                }

                if (i == ENTRY_BY_CLUSTER)
                {
                    printf("O diretorio \"%s\" não existe\n", file);
                    break;
                }

                file = next;
            }
        }
        else if (strcmp(command, "dump") == 0)
        {
            /*printf("Entrei\n");
            printf("root[0]: %i\n", root_dir[0].size);
            printf("fat[0]: %i\n", fat[0]);
            int a = 10;
            int b = 5;
            int x;
            int numero = 10;
            numero >= 0 ? numero++ : numero--;
            a == b ? (x = a) : (x = b);
            printf("%d\n", x);
            printf("free_clusters[0]: %i\n", free_clusters[0]);
            char *parentDir = strtok(NULL, "/");
            int i = 0;
            while (parentDir != NULL)
            {
                printf("%d -> %s\n", i++, parentDir);
                parentDir = strtok(NULL, "/");
            }
            int cluster = 1;
            FILE *file = fopen(FAT_NAME, "rb+");
            fseek(file, (CLUSTER_SIZE), SEEK_SET);
            uint16_t test;
            fread(&test, sizeof(test), 1, file);
            printf("0x%x\n", test);
            char *stream = strtok(NULL, "\"");
            //stream += 1;
            //stream[strlen(stream) - 1] = '\0';
            printf("String → %s\n", stream);
            char *path = strtok(NULL, " ");
            printf("Path -> '%s'\n", path);
            char *meajudadeus = strtok(path, "/");
            printf("Path -> '%s'\n", meajudadeus);
            meajudadeus = strtok(NULL, "/");
            printf("Path -> '%s'\n", meajudadeus);
            printf("tam stream -> %d\n", sizeof(stream));
            printf("tam char -> %d\n", sizeof(char));*/
            printf("\\0 -> %x\n", '\0');
        }

        else
        {
            printf("Comando inválido!\n");
        }
    }
}