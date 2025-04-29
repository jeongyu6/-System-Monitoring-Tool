#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h> //used for usleep
#include <ctype.h> //used for detecting space between strings
#include <sys/resource.h> // used for cpu utilization calculations
#include <sys/sysinfo.h> // used to retrieve system memory details

typedef struct
{
    bool memory_flag;
    bool cpu_flag;
    bool cores_flag;
    int samples;
    unsigned long tdelay;
    bool updated_sample;
    bool updated_tdelay;
    int argc;
    char **argv;
} ArgsInfo;

typedef struct
{
    int row;
    int col;
} CursorPosition;

/**
 * Initializes the `ArgsInfo` structure with default values.
 * 
 * This function dynamically allocates memory for the `ArgsInfo` structure and 
 * initializes its fields. It sets default values for the number of samples and 
 * time delay while ensuring that flags for memory, CPU, and core usage are 
 * initially disabled. The `updated_sample` and `updated_tdelay` fields are set 
 * to `false` to prevent the user from specifying multiple values for `samples` 
 * and `tdelay`.
 * 
 * Behavior:
 * - `samples` is set to 20 (default sample count).
 * - `tdelay` is set to 500,000 microseconds (default time delay).
 * - `memory_flag`, `cpu_flag`, and `cores_flag` are set to `false` (disabled by default).
 * - `updated_sample` and `updated_tdelay` are set to `false` to track whether 
 *   user-specified values have been assigned.
 * - The `argc` and `argv` fields store the command-line arguments.
 * 
 * @return A pointer to an initialized `ArgsInfo` structure.
 *         Exits the program if memory allocation fails.
 */
ArgsInfo* initializeArgument(int argc, char **argv){
    ArgsInfo *argsInfo = (ArgsInfo *)malloc(sizeof(ArgsInfo));
    if (argsInfo == NULL)
    {
        fprintf(stderr, "Error:Memory allocation\n");
        exit(1);
    }
    argsInfo->argc = argc;
    argsInfo->argv = argv;
    argsInfo->cores_flag = false;
    argsInfo->cpu_flag = false;
    argsInfo->memory_flag = false;
    argsInfo->samples = 20;    // default values
    argsInfo->tdelay = 500000; // default values
    argsInfo->updated_sample = false;
    argsInfo->updated_tdelay = false;
    return argsInfo;
}

/**
 * Removes leading whitespace characters from a given string.
 * Uses the isspace function to check for spaces and moves the pointer
 * forward to the first non-space character.
 * @param str The input string.
 * @return A pointer to the modified string with leading spaces removed,
 *         or NULL if the input is NULL.
 */
char *removeWhiteSpace(char *str)
{
    if (str == NULL)
    {
        return NULL;
    }
    while (isspace((unsigned char)*str))
    {
        str++;
    }
    return str;
}

/**
 * Stores a cursor position (row and column) for terminal-based graph plotting.
 * Used to track positions for elements like headers, memory, and CPU plots,
 * as well as terminal navigation using escape codes.
 * @param row The row position in the terminal.
 * @param col The column position in the terminal.
 * @return A CursorPosition struct containing the given row and column values.
 */
CursorPosition save_position(int row, int col)
{
    CursorPosition pos;
    pos.row = row;
    pos.col = col;
    return pos;
}

/**
 * Prints a specified number of newline characters to move the cursor down.
 * 
 * This function iterates and prints newline characters to shift the terminal output 
 * down by the specified number of lines.
 * 
 * @param num_lines The number of lines to move down.
 */
void change_line(int num_lines)
{
    for (int i = 0; i < num_lines; i++)
    {
        printf("%s", "\n");
    }
}

/**
 * Gets the total memory using 'sysinfo()'
 * This function fetches the total RAM available in the system 
 * and converts it from bytes to gigabytes.
 * 
 * @return The total system memory in gigabytes as a long double.
 *         Exits the program if `sysinfo()` fails.
 */
long double get_total_memory()
{
    struct sysinfo info;
    if (sysinfo(&info) == -1)
    {
        fprintf(stderr, "Error: Cannot read system memory information\n");
        exit(1);
    }
    long double total_memory = (info.totalram * info.mem_unit) / (1024.0 * 1024.0 * 1024.0); 
    if(total_memory < 0){
        fprintf(stderr,"Error:Total memory cannot be negative\n");
    }
    return total_memory;
}

/**
 * Calculates the CPU utilization percentage over time.
 * 
 * This function reads CPU time statistics from `/proc/stat` and calculates the 
 * CPU utilization using the following method:
 * 
 * 1. **Total CPU Utilization Time** (Tᵢ) is computed as:
 *      Tᵢ = user + nice + system + idle + iowait + irq + softirq + steal;
 * 
 * 2. **Idle Time** (Iᵢ) consists of:
 *      Iᵢ = idle + iowait;
 * 
 * 3. **CPU Usage Time** (Uᵢ) at time i is determined by:
 *      Uᵢ = Tᵢ - Iᵢ;
 * 
 * 4. **CPU Utilization Calculation**:
 *    The CPU utilization between two sampling times is given by:
 * 
 *        CPU Utilization (%) = ((U₂ - U₁) / (T₂ - T₁)) * 100;
 * 
 *    Where:
 *    - **T₁, I₁, U₁** represent total, idle, and usage times at the first sampling.
 *    - **T₂, I₂, U₂** represent total, idle, and usage times at the second sampling.
 *    - **U₂ - U₁** is the change in CPU usage.
 *    - **T₂ - T₁** is the change in total CPU time.
 * 
 * The function updates the previous total and idle CPU values for subsequent calculations.
 * 
 * @param preTotalCPU A pointer to store the previous total CPU time.
 * @param preIdleCPU A pointer to store the previous idle CPU time.
 * @param finalTotalCPU A pointer to store the current total CPU time.
 * @param finalIdleCPU A pointer to store the current idle CPU time.
 * @return The CPU utilization as a percentage. Returns 0 if no valid data is available.
 */
long double calculate_cpu_utilization(long double *preTotalCPU, long double *preIdleCPU, long double *finalTotalCPU, long double *finalIdleCPU)
{
    char input_string[1024];
    long double user, nice, system, idle, iowait, irq, softirq, steal;

    FILE *fp = fopen("/proc/stat", "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Error: Failed opening /proc/stat\n");
        return 0;
    }

    fscanf(fp, "%s %Lf %Lf %Lf %Lf %Lf %Lf %Lf %Lf",
           input_string, &user, &nice, &system, &idle,
           &iowait, &irq, &softirq, &steal);

    if (fclose(fp) != 0)
    {
        fprintf(stderr, "Error: Failed to close file\n");
        return -1;
    }

    // Compute total and idle CPU times
    long double currentTotalCPU = user + nice + system + idle + iowait + irq + softirq + steal;
    long double currentIdleCPU = idle + iowait;

    // First run initialization
    if (*preTotalCPU == 0 && *preIdleCPU == 0)
    {
        *preTotalCPU = currentTotalCPU;
        *preIdleCPU = currentIdleCPU;
        return 0;
    }

    long double U_initial = *preTotalCPU - *preIdleCPU;
    *finalTotalCPU = currentTotalCPU;
    *finalIdleCPU = currentIdleCPU;
    long double U_final = *finalTotalCPU - *finalIdleCPU;

    // Compute deltas
    long double delta_total = U_final - U_initial;
    long double delta_idle = *finalTotalCPU - *preTotalCPU;

    // Update stored values for next iteration
    *preTotalCPU = currentTotalCPU;
    *preIdleCPU = currentIdleCPU;

    if (delta_total == 0 || delta_idle == 0)
    {
        return 0;
    }
    return (delta_total / delta_idle) * 100.0;
}

/**
 * Calculates the amount of free memory available in the system.
 * 
 * This function uses 'sysinfo()' to retrieve the "freeram" value, 
 * converts it from kilobytes to gigabytes, and subtracts it from total memory 
 * and then returns the available memory.
 * 
 * @return The amount of used system memory in gigabytes as a long double.
 *         Returns 0 if the sysinfo() fails.
 */
long double calculate_memory_used()
{
    struct sysinfo info;
    if (sysinfo(&info) == -1)
    {
        fprintf(stderr, "Error: Cannot read memory used file\n");
        return 0;
    }
    long double freeMemory = (info.freeram * info.mem_unit)/(1024.0 * 1024.0 * 1024.0);
    return (get_total_memory()- freeMemory);
}

/**
 * Retrieves the maximum CPU frequency from the system.
 * 
 * This function reads the `/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq` 
 * file to determine the highest supported CPU frequency, converting it from 
 * kilohertz to gigahertz.
 * 
 * @return The maximum CPU frequency in gigahertz as a long double.
 *         Returns 0 if the file cannot be accessed.
 */
long double calculate_max_frequency()
{ 
    FILE *fp = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Error reading the file\n");
        return 0;
    }
    long double max_freq;
    if (fscanf(fp, "%Lf", &max_freq) == 1)
    {
        fclose(fp);
        return max_freq / 1000000;
    }
    else
    {
        fclose(fp);
        fprintf(stderr, "Error: failed to calculate max frequency\n");
        exit(1);
    }
}

/**
 * Counts the number of CPU cores available on the system.
 * 
 * This function reads the `/proc/cpuinfo` file and counts occurrences of the 
 * "processor" keyword, which indicates the total number of logical CPU cores.
 * 
 * @return The total number of CPU cores.
 *         Returns -1 if the file cannot be accessed or closed improperly.
 */
int calculate_cores()
{
    char input_string[1024];
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Error reading the file\n");
        return -1;
    }

    int cores = 0;
    while (fgets(input_string, sizeof(input_string), fp))
    {
        if (strncmp("processor", input_string, 9) == 0)
        {
            cores++;
        }
    }
    if (fclose(fp) != 0)
    {
        fprintf(stderr, "Error: Failed to close file\n");
        return -1;
    }
    return cores;
}

/**
 * Determines if the current command-line argument (CLA) is a positional argument.
 * 
 * In the CLA syntax, positional arguments `[samples [tdelay]]` must appear before 
 * flagged arguments (`--memory`, `--cpu`, `--cores`, `--samples=N`, `--tdelay=T`).
 * This function validates whether an argument is positional, ensuring `samples` 
 * and `tdelay` appear immediately after the executable name.
 * 
 * @param argsInfo A pointer to the structure containing command-line arguments.
 * @param current_index A pointer to the current argument index being processed.
 * @return `true` if the argument is valid and assigned to `samples` or `tdelay`; 
 *         otherwise, `false` if the value is invalid, NULL, or out of order.
 */
bool isPositional(ArgsInfo *argsInfo, int current_index)
{
    char* arg = argsInfo->argv[current_index];
    while (*arg) {
        if (!isdigit((unsigned char)*arg)) // Check if character is not a digit
            return false;
        arg++;
    }
    return true;
}

/**
 * Identifies and processes command-line flag arguments.
 * 
 * This function checks if the given argument is a recognized flag 
 * (`--memory`, `--cpu`, `--cores`, `--samples=N`, `--tdelay=T`). If a flag is detected, 
 * it updates the corresponding field in the `argsInfo` structure.
 * 
 * @param argsInfo A pointer to the structure containing command-line arguments.
 * @param current_index A pointer to the current argument index being processed.
 * @return `true` if a valid flag is detected and processed; 
 *         `false` if the argument is invalid or missing required values.
 */
bool isFlag(ArgsInfo *argsInfo, int *current_index)
{
    if (argsInfo == NULL || strlen(argsInfo->argv[*current_index]) == 0)
    {
        return false;
    }
    char *argv = argsInfo->argv[*current_index];
    if (strcmp(argv, "--memory") == 0)
    {
        argsInfo->memory_flag = true;
        return true;
    }
    else if (strcmp(argv, "--cpu") == 0)
    {
        argsInfo->cpu_flag = true;
        return true;
    }
    else if (strcmp(argv, "--cores") == 0)
    {
        argsInfo->cores_flag = true;
        return true;
    }
    else if (strncmp(argv, "--samples=", 10) == 0)
    {
        char *value_str = argv + 10;
        printf("here");
        if (value_str == NULL || *value_str == '\0')
        {
            fprintf(stderr, "Error: Missing value\n");
            return false;
        }

         printf("here2");
        if(argsInfo->updated_sample == true){
            fprintf(stderr,"Error: cannot have multiple sample values\n");
            return false;
        }
        printf("here3");
        value_str = removeWhiteSpace(value_str);
        char *endptr;
        long value = strtol(value_str, &endptr, 10);

        if(*endptr != '\0' || value <= 0){
           fprintf(stderr,"Error: Invalid value for --samples\n");
            return false;
        }
        else{
            argsInfo->samples = (int)value;
            argsInfo->updated_sample = true;
            return true;
        }
    }
    else if (strncmp(argv, "--tdelay=", 9) == 0)
    {
        char *value_str = argv + 9;
         if (value_str == NULL || *value_str == '\0')
        {
            fprintf(stderr, "Error: Missing value\n");
            return false;
        }

        if(argsInfo->updated_tdelay == true){
            fprintf(stderr,"Error: cannot have multiple tdelay values\n");
            return false;
        }

        value_str = removeWhiteSpace(value_str);
        char *endptr;
        long value = strtol(value_str, &endptr, 10);

        if (*endptr != '\0' || value <= 0)
        {
           fprintf(stderr, "Error:Invalid value for tdelay\n");
            return false;
        }
        else{
            argsInfo->tdelay = value;
            argsInfo->updated_tdelay = true;
            return true;
        }
    }
    return false;
}


/**
 * Parses and processes command-line arguments to configure program settings.
 * 
 * This function determines which flags and positional arguments are provided 
 * by the user. It ensures the correct order of arguments and applies default 
 * values when necessary.
 * 
 * Behavior:
 * - If no arguments are given, it enables memory, CPU, and core monitoring by default.
 * - If arguments are provided:
 *   - Positional arguments (samples, tdelay) must appear first.
 *   - Flag arguments (--memory, --cpu, --cores, --samples=N, --tdelay=T) follow.
 *   - Errors are triggered for unknown or misplaced arguments.
 * - If no valid flag is set, all flags (memory, CPU, cores) are enabled by default.
 * 
 * @param argc The number of command-line arguments.
 * @param argsInfo A pointer to the structure storing argument flags and values.
 */
int processCommandLineArguments(int argc, ArgsInfo* argsInfo){
    if (argc == 1){
        argsInfo->cores_flag = argsInfo->cpu_flag = argsInfo->memory_flag = true;
    } else
    {
        int current_index = 1;
        if(argc >= 2 && isPositional(argsInfo,1)){
            char *endptr;
            long value = strtol(argsInfo->argv[1], &endptr, 10);
            if(*endptr != '\0' || value <=0){
                fprintf(stderr, "Error: invalid value for samples\n");
                return -1;
            }

            argsInfo->samples = (int)value;
            argsInfo->updated_sample = true;
            current_index++;
            if (argc >= 3 && isPositional(argsInfo,2)) {
                value = strtol(argsInfo->argv[2], &endptr, 10);
                if(*endptr != '\0' || value <=0){
                    fprintf(stderr,"Error: invalid value for tdelay\n");
                    return -1;
                }
                argsInfo->tdelay = (unsigned long)value;
                argsInfo->updated_tdelay = true;
                current_index++;
            }
        }
        for (;current_index < argc; current_index++)
        {
            if (!isFlag(argsInfo, &current_index))
            {
                fprintf(stderr, "Error: Unknown argument\n");
                return -1;
            }
        }
    }
    if(argsInfo->cores_flag == false && argsInfo->cpu_flag == false && argsInfo->memory_flag == false){
        argsInfo->cores_flag = true;
        argsInfo->cpu_flag = true;
        argsInfo->memory_flag = true;
    }
    return 0;
}


/**
 * Draws the initial structure of the graph, including the label, unit, height, 
 * and baseline, based on available memory usage, CPU utilization, or sample count.
 * 
 * The function prints the graph's heading and vertical label, allowing values 
 * to be updated dynamically in real-time based on collected data. It then draws 
 * the y-axis and horizontal axis using escape codes specified in the assignment handout.
 * 
 * 
 * @param label The title of the graph.
 * @param unit The measurement unit (e.g., total memory in GB or CPU utilization in %).
 * @param height The height of the y-axis.
 * @param baseline The starting value for the y-axis (e.g., "0GB" or "0%").
 * @param current_row A pointer to the current row position in the terminal.
 * @param current_column A pointer to the current column position in the terminal.
 * @param samples The number of data points to be plotted (minimum 20 for proper scaling).
 * @return The cursor position where real-time updates should be applied.
 */
CursorPosition draw_graph(const char *label, const char *unit, int height, const char *baseline, int *current_row, int *current_column, int samples)
{
    printf("\033[%d;%dH%s\n", *current_row, *current_column, label);
    CursorPosition pos = save_position(*current_row, *current_column + strlen(label) + 1);

    // Move down 1 line to print the maximum unit
    *(current_row) += 1;
    printf("\033[%d;%dH %s", *current_row, *current_column, unit);

    //align the rest of the graph 8 columns after the maximum unit
    *(current_column) += 8;

    for (int i = 0; i < height; i++)
    {
        printf("\033[%d;%dH|\n", *current_row + i, *current_column);
    }
    *(current_row) += height;

    // Print label for the baseline
    printf("\033[%d;%dH %s", *current_row, (int)(*(current_column) - strlen(baseline) - 2), baseline);

    //** */ Print horizontal axis: If samples is smaller than 20, set the default samples to 20 to create a graph
    if (samples < 20)
    {
        samples = 20;
    }
    for (int j = 0; j < samples + 1; j++)
    {
        printf("\033[%d;%dH\u2500", *(current_row), *(current_column) + j);
    }

    printf("%s", "\n");
    *current_row += 1;

    return pos;
}

/**
 * Draws the memory usage graph with a fixed scale from 0 to 10.
 * 
 * This function calculates a scaling factor based on the total system memory, 
 * ensuring that each unit on the y-axis represents an appropriate memory increment. 
 * It then calls `draw_graph` to generate a memory graph with proper labeling.
 * 
 * @param current_row A pointer to the current row position in the terminal.
 * @param current_column A pointer to the current column position in the terminal.
 * @param scalefactor A pointer to store the scaling factor for memory increments.
 * @param samples The total number of samples to be plotted.
 * @return The cursor position after drawing the memory graph.
 */
CursorPosition draw_memory_graph(int *current_row, int *current_column, long double *scalefactor, int samples)
{
    long double max_memory = get_total_memory();

    int height = 10;
    *scalefactor = max_memory / height;

    char unit[20];
    sprintf(unit, "%.Lf GB",max_memory);
    return draw_graph("v Memory ", unit, height, "0 GB", current_row, current_column, samples);
}

/**
 * Draws the CPU utilization graph with a fixed scale from 0% to 100%.
 * 
 * This function creates a CPU usage graph with a height of 11 units, 
 * representing percentage increments. It calls `draw_graph` to generate 
 * the graph with appropriate labels.
 * 
 * 1st unit = 0% to 9%
 * 2nd unit = 10% to 19%
 * ......(and so on)
 * 11th unit = 100%
 * 
 * @param start_row A pointer to the current row position in the terminal.
 * @param start_column A pointer to the current column position in the terminal.
 * @param samples The total number of samples to be plotted.
 * @return The cursor position after drawing the CPU graph.
 */
CursorPosition draw_cpu_graph(int *start_row, int *start_column, int samples)
{
    int height = 11;
    return draw_graph("v CPU ", "100%", height, "0%", start_row, start_column, samples);
}


/**
 * Draws a visual representation of CPU cores in a grid format.
 * 
 * This function prints the number of available CPU cores along with their 
 * maximum frequency. It then visualizes the cores as small box-like units 
 * arranged in rows and columns.
 * 
 * The layout consists of:
 * - A heading displaying the number of cores and their frequency.
 * - A grid where each core is represented as a boxed unit.
 * 
 * The function dynamically adjusts the number of rows based on the total number 
 * of cores, ensuring a maximum of 4 cores per row for readability.
 * 
 * Structure:
 * ```
 * +───+ +───+ +───+ +───+
 * |   | |   | |   | |   |
 * +───+ +───+ +───+ +───+
 * ```
 * 
 * @param coresNumber The total number of CPU cores.
 * @param currentcol A pointer to the current column position in the terminal.
 * @param currentrow A pointer to the current row position in the terminal.
 * @param frequency The maximum CPU frequency in GHz.
 * @return A `CursorPosition` struct representing the final cursor position after drawing.
 */
CursorPosition coresGraph(int coresNumber, int *currentcol, int *currentrow, double frequency)
{
    (*currentrow) += 2;
    printf("\033[%d;%dHv Number of Cores: %d @ %.2f GHz\n", *currentrow, *currentcol, coresNumber, frequency);
    (*currentrow)++;

    char *top = "+───+";
    char *middle = "|   |";

    int cols = 4;
    int rows = (coresNumber + cols - 1) / cols;
    int width = 6;

    for (int row = 0; row < rows; row++)
    {
        for (int col = 0; col < cols; col++)
        {
            if (row * cols + col < coresNumber)
            {
                printf("\033[%d;%dH%s", *currentrow + row * 2, *currentcol + col * width, top);
            }
        }
        printf("\n");

        for (int col = 0; col < cols; col++)
        {
            if (row * cols + col < coresNumber)
            {
                printf("\033[%d;%dH%s", *currentrow + row * 2 + 1, *currentcol + col * width, middle);
            }
        }
        printf("\n");

        for (int col = 0; col < cols; col++)
        {
            if (row * cols + col < coresNumber)
            {
                printf("\033[%d;%dH%s", *currentrow + row * 2 + 2, *currentcol + col * width, top);
            }
        }

        (*currentrow)++;
        printf("\n");
    }
    (*currentrow) += rows * 2 + 2;
    CursorPosition pos = save_position(*currentrow, 1);
    return pos;
}


/**
 * Main point of the system monitoring program.
 * 
 * This program monitors and displays system statistics such as:
 * - Memory usage
 * - CPU utilization
 * - Number of CPU cores and their frequency
 * 
 * The program processes command-line arguments, determines what information 
 * to display, and dynamically updates system metrics at user-defined intervals.
 * 
 * Execution Flow:
 * 1. Parse and validate command-line arguments.
 * 2. Initialize the display and draw graphs based on user flags.
 * 3. Continuously collect and update memory/CPU utilization data.
 * 4. If enabled, display CPU core information at the end.
 * 5. Restore the terminal cursor and free allocated memory.
 * 
 * Command-line Arguments:
 * - Positional Arguments:
 *   - `[samples]`  → Number of data samples to collect (default: 20).
 *   - `[tdelay]`   → Delay in microseconds between samples (default: 500,000).
 * - Flags:         
 *   - `--memory`   → Display memory usage graph.
 *   - `--cpu`      → Display CPU utilization graph.
 *   - `--cores`    → Display the number of CPU cores and their max frequency.
 *   - `--samples=N` → Specify number of samples.
 *   - `--tdelay=T`  → Specify time delay between samples.
 * 
 * @param argc The number of command-line arguments.
 * @param argv The array of command-line argument strings.
 * @return Returns 0 upon successful execution.
 */
int main(int argc, char **argv)
{
    ArgsInfo * argsInfo = initializeArgument(argc,argv);

    if(argsInfo == NULL){
        fprintf(stderr,"Error: failed to initialize arguments");
    }

    if(processCommandLineArguments(argc, argsInfo) == -1){
        free(argsInfo);
        exit(1);
    }

    CursorPosition memory;
    CursorPosition memory_heading;
    CursorPosition cpu;
    CursorPosition cpu_heading;
    CursorPosition ending_position;
   
    printf("\033c");  // Resets screen
    printf("\033[H"); // position cursor at the top left corner
    // printf("new run");
    fflush(stdout);

    double seconds = argsInfo->tdelay / 1000000.0;
    printf("Nbr of samples: %d -- every %lu microSecs (%f secs)", argsInfo->samples, argsInfo->tdelay, seconds);
    fflush(stdout);

    int current_column = 1;
    int current_row = 1;
    long double scaling_factor = 0.0;

    if (argsInfo->memory_flag)
    {
        current_row += 1;
        memory_heading = draw_memory_graph(&current_row, &current_column, &scaling_factor, argsInfo->samples);
        memory = save_position(current_row - 1, current_column + 1);
    }

    if (argsInfo->cpu_flag)
    {
        const int NUM_NEW_LINES = 2;
        change_line(NUM_NEW_LINES);
        current_row += NUM_NEW_LINES;
        current_column = 1;

        cpu_heading = draw_cpu_graph(&current_row, &current_column, argsInfo->samples);
        cpu = save_position(current_row - 1, current_column + 1);
    }

    ending_position = save_position(current_row, current_column);

    long double preTotalCPU = 0;
    long double preIdleCPU = 0;
    long double finalTotalCPU = 0;
    long double finalIdleCPU = 0;
    double max_frequency = 0.0;


    calculate_cpu_utilization(&preTotalCPU, &preIdleCPU, &finalTotalCPU, &finalIdleCPU);
    for (int i = 0; i < argsInfo->samples; i++)
    {
        if (argsInfo->memory_flag)
        {
            long double memory_used = calculate_memory_used();

            printf("\033[%d;%dH       ", memory_heading.row, memory_heading.col); 
            fflush(stdout);
            
            char *unit = "GB";
            printf("\033[%d;%dH %.2Lf %s", memory_heading.row, memory_heading.col, memory_used,unit);
            fflush(stdout);
            
            printf("\033[%d;%dH#", memory.row - (int)(memory_used / scaling_factor) - 1, memory.col++);
            fflush(stdout);
        }
        if (argsInfo->cpu_flag)
        {
            long double cpu_utilization = calculate_cpu_utilization(&preTotalCPU, &preIdleCPU, &finalTotalCPU, &finalIdleCPU);

            printf("\033[%d;%dH %.2Lf %%          ", cpu_heading.row, cpu_heading.col, cpu_utilization);
            fflush(stdout);


            printf("\033[%d;%dH:",cpu.row - (int)(cpu_utilization / 10) - 1, cpu.col++);
            fflush(stdout);
        }
        usleep(argsInfo->tdelay);
    }

    ending_position = save_position(current_row, 1);
 
    if (argsInfo->cores_flag)
    {
        max_frequency = calculate_max_frequency();
        current_column = 1;
        current_row += 1;
        ending_position = coresGraph(calculate_cores(), &current_column, &current_row, max_frequency);
    }

    printf("\033[%d;%dH", ending_position.row, ending_position.col);
    free(argsInfo);
    return 0;
}