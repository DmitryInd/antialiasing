#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>



struct pixel {
	unsigned char red_part;
	unsigned char green_part;
	unsigned char blue_part;
};

/*Считывает положительное целое число из строки, записывая его в int
Возращает указаетль на конец числа*/
unsigned char* get_int(unsigned char* str, int *number) {
	while(*str < '0' || *str > '9')
		str++;

	while(*str >= '0' && *str <= '9') {
		(*number) *= 10;
		(*number) += *str - 48;
		str++;
	}

	return str;
}

/*Считывает положительное дробное число из строки, записывая его в int
Возращает указаетль на конец числа*/
unsigned char* get_double(unsigned char* str, double *number) {
	while(*str < '0' || *str > '9')
		str++;

	int amount = 0; //10^{Число знаков после запятой}
	while((*str >= '0' && *str <= '9') || (*str == '.' && amount == 0)) {
		if (*str == '.')
			amount = 1;

		if (amount > 0)
			amount *= 10;

		(*number) *= 10;
		(*number) += *str - 48;
		str++;
	}
	if (amount != 0)
		(*number) /= amount;

	return str;
}

//Считывает матрицу свёртки из файла
double** get_matrix(int d_matrix, int *width, int *height) {
	struct stat stat_buffer;
	fstat(d_matrix, &stat_buffer);
	unsigned char* matrix_ptr = mmap(NULL, stat_buffer.st_size, PROT_READ, MAP_SHARED, d_matrix, 0);
	matrix_ptr = get_int(matrix_ptr, width) + 1;
	matrix_ptr = get_int(matrix_ptr, height) + 1;
	double** matrix = malloc((*height) * sizeof(double*));
	for (int i = 0; i < (*height); i++)
		matrix[i] = malloc((*width) * sizeof(double));

	for(int i = 0; i < *height; i++)
		for(int j = 0; j < *width; j++)
			matrix_ptr = get_double(matrix_ptr, &matrix[i][j]) + 1;

	munmap(matrix_ptr, stat_buffer.st_size);
	return matrix;
}

//Применяет матрицу свёртки на конкретной компоненте указанного пикселя
unsigned char modify(unsigned char* pixel_start, unsigned char* pixel_end,
                     unsigned char* string_left, int pixel_width,
                     short int cell_length, unsigned char* value_ptr,
                     double** matrix, int width, int height) {
	int y_center = height/2; //Позиция центрального элемента по y
	int x_center = width/2; //Позиция центрального элемента по x
	int weight_sum = 0; //Сумма всех элементов в массиве свёртки
	int new_value = 0; //Новое значение составляющей без нормирования
	for (int i = 0; i < height; i++)
		for (int j = 0; j < width; j++) {
			unsigned char* new_value_ptr = value_ptr + (j - x_center)*
                                                           cell_length;
			/*Проверяем искомый пиксель на существование в строке
			(он должен нахолится в пределах строки)*/
			if (new_value_ptr >= string_left &&
				new_value_ptr <= string_left + pixel_width * cell_length) {
				new_value_ptr += (i - y_center) * pixel_width * cell_length;
				/*Проверка на существование адресса файле
                (аналогично проверке на существование в столбце)*/
				if (new_value_ptr >= pixel_start && new_value_ptr <= pixel_end) {
					weight_sum += matrix[i][j];
					new_value += (*new_value_ptr) * matrix[i][j];
				}
			}
		}

	if (new_value < 0)
		new_value = 0;

	return (weight_sum == 0? 0: (unsigned char)(new_value/weight_sum));
}

/*Функция сглаживания, на вход подаётся fd входного и
 выходного изображения и матрица свёрстки с параметрами*/
void antialiasing(int d_input, int d_output, double** matrix, int  m_width, int m_height) {
	//Считываение оригинального изображения и копирование его
	struct stat stat_buffer;
	fstat(d_input, &stat_buffer);
	unsigned char* input_ptr = mmap(NULL, stat_buffer.st_size, PROT_READ, MAP_SHARED, d_input, 0);
	unsigned char* output_ptr = mmap(NULL, stat_buffer.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, d_output, 0);
	memcpy(output_ptr, input_ptr, stat_buffer.st_size);

	//Здесь начинается полноценное чтение файла
	int width = abs(*((int*)(input_ptr + 18)));
	int height = abs(*((int*)(input_ptr + 22)));
	//Считывается кол-во бит, выделяемых на пиксель
	short int cell_length = *(short int*)(input_ptr + 28);
	cell_length /= 8; //Храним длинну в байтах
	//Относительное начало информации о пикселях
	long long pixel_start = *((int*)(input_ptr + 10));
	long long pixel_end = width * height * cell_length + pixel_start;
	//Левая и правая граница строки
	unsigned char* input_pixel_left = input_ptr + pixel_start;
	unsigned char* input_pixel_start = input_ptr + pixel_start;
	unsigned char* input_pixel_end = input_ptr + pixel_end;
	for (long long index = pixel_start; index < pixel_end; index += cell_length) {
		if (input_ptr + index >= input_pixel_left + width * cell_length)
			input_pixel_left += width * cell_length;

		struct pixel new_value =
        {modify(input_pixel_start, input_pixel_end, input_pixel_left, width, cell_length,
                                  input_ptr + index, matrix, m_width, m_height),
         modify(input_pixel_start, input_pixel_end, input_pixel_left, width, cell_length,
                              input_ptr + index + 1, matrix, m_width, m_height),
         modify(input_pixel_start, input_pixel_end, input_pixel_left, width, cell_length,
                             input_ptr + index + 2, matrix, m_width, m_height)};
		*((struct pixel*)(output_ptr + index)) = new_value;
	}

	munmap(input_ptr, stat_buffer.st_size);
	munmap(output_ptr, stat_buffer.st_size);
}

//Считывание матрицы из файла
void matrix_mode(int d_input, int d_output) {
	unsigned char matrix_file_name[4096];
	printf("%s\n", "Введите путь к матрице свёртки:");
	scanf("%s", matrix_file_name);
	int d_matrix = open(matrix_file_name, O_RDONLY);
	if (d_matrix < 0) {
		perror("Не удаётся открыть файл с матрицей свёртки");
		return;
	}
	int m_width, m_height;
	double** matrix = get_matrix(d_matrix, &m_width, &m_height);
	antialiasing(d_input, d_output, matrix, m_width, m_height);
	free(matrix);
	close(d_matrix);
}

//Считывание матрицы по трём значениям
void number_mode(int d_input, int d_output) {
	printf("%s\n", "Введите три значения матрицы свёртки:");
	double** matrix = malloc(3 * sizeof(double*));
	for (int i = 0; i < 3; i++)
		matrix[i] = malloc(3 * sizeof(double));

	double value = 0;
	//Среднее значение
	scanf("%lf", &value);
	matrix[1][1] = value;
	//Боковое значение
	scanf("%lf", &value);
	matrix[0][1] = value;
	matrix[1][0] = value;
	matrix[1][2] = value;
	matrix[2][1] = value;
	//Угловое значение
	scanf("%lf", &value);
	matrix[0][0] = value;
	matrix[0][2] = value;
	matrix[2][0] = value;
	matrix[2][2] = value;

	antialiasing(d_input, d_output, matrix, 3, 3);
	free(matrix);
}

int main(int argc, char** argv) {
	unsigned char input_file_name[4096];
	unsigned char output_file_name[4106];
	printf("%s\n", "Введите путь к изображению:");
	scanf("%s", input_file_name);
	int length = strlen(input_file_name);
	memcpy(output_file_name, input_file_name, length - 4);
	memcpy(output_file_name + length - 4, "_processed.bmp", 14);
	int d_input = open(input_file_name, O_RDONLY);
	if (d_input == -1) {
		perror("Файл не открывается");
		return 1;
	}
	int d_output = open(output_file_name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	struct stat stat_buffer;
	fstat(d_input, &stat_buffer);
	lseek(d_output, stat_buffer.st_size - 1, SEEK_SET);
	write(d_output, "\n", 1);
	close(d_output);
	d_output = open(output_file_name, O_RDWR, 0666);
	if (d_output == -1) {
		perror("Не получилось создать обработанную копию файла");
		return 1;
	}
	printf("%s\n", "Ввыберите тип ввода матрицы свёртки:\n1) Через файл\n2) Через значения");
	int answer = 0;
	scanf("%i", &answer);
	switch(answer) {
		case 1:
			matrix_mode(d_input, d_output);
			break;
		case 2:
			number_mode(d_input, d_output);
			break;
		default:
			printf("%s\n", "Такого варианта нет");
			break;
	}
	close(d_input);
	close(d_output);
	return 0;
}
