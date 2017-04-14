#include <fcntl.h>
#include <getopt.h>
#include <libintl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <vector>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "error.h"

#define min(x, y) ((x) < (y) ? (x) : (y))

// only required when using a real serial port
void setser(int fd) 
{
	struct termios newtio;

	if (tcgetattr(fd, &newtio) == -1)
		error_exit(true, "tcgetattr failed");

	newtio.c_iflag = IGNBRK; // | ISTRIP;
	newtio.c_oflag = 0;
	newtio.c_cflag = B9600 | CS8 | CREAD | CLOCAL | CSTOPB;
	newtio.c_lflag = 0;
	newtio.c_cc[VMIN] = 1;
	newtio.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSANOW, &newtio) == -1)
		error_exit(true, "tcsetattr failed");

	tcflush(fd, TCIOFLUSH);
}

void autosend(const int fd, const bool state)
{
	uint8_t b = state ? 0x47 : 0x48;

	if (write(fd, &b, 1) <= 0)
		error_exit(true, "Problem sending command to powerbank");
}

#define SWITCHES_COLUMN_WIDTH	24

int max_x = 80, max_y = 24;

void determine_terminal_size(void)
{
	struct winsize size;

	max_x = max_y = 0;

	if (!isatty(1))
	{
		max_y = 24;
		max_x = 80;
	}
#ifdef TIOCGWINSZ
	else if (ioctl(1, TIOCGWINSZ, &size) == 0)
	{
		max_y = size.ws_row;
		max_x = size.ws_col;
	}
#endif

	if (!max_x || !max_y)
	{
		char *dummy = getenv("COLUMNS");
		if (dummy)
			max_x = atoi(dummy);
		else
			max_x = 80;

		dummy = getenv("LINES");
		if (dummy)
			max_y = atoi(dummy);
		else
			max_y = 24;
	}
}

void str_add(char **to, const char *what, ...)
{
	int len_to = *to ? strlen(*to) : 0;
	char *buffer = NULL;
	int len_what = 0;

	va_list ap;

	va_start(ap, what);
	len_what = vasprintf(&buffer, what, ap);
	va_end(ap);

	*to = (char *)realloc(*to, len_to + len_what + 1);

	memcpy(&(*to)[len_to], buffer, len_what);

	(*to)[len_to + len_what] = 0x00;

	free(buffer);
}

void format_help(const char *short_str, const char *long_str, const char *descr)
{
	int par_width = SWITCHES_COLUMN_WIDTH, max_wrap_width = par_width / 2, cur_par_width = 0;
	int descr_width = max_x - (par_width + 1);
	char *line = NULL, *p = (char *)descr;
	char first = 1;

	if (long_str && short_str)
		str_add(&line, "%-4s / %s", short_str, long_str);
	else if (long_str)
		str_add(&line, "%s", long_str);
	else
		str_add(&line, "%s", short_str);

	cur_par_width = fprintf(stderr, "%-*s ", par_width, line);

	free(line);

	if (par_width + 1 >= max_x || cur_par_width >= max_x)
	{
		fprintf(stderr, "%s\n", descr);
		return;
	}

	for(;strlen(p);)
	{
		char *n =  NULL, *kn = NULL, *copy = NULL;
		int n_len = 0, len_after_ww = 0, len_before_ww = 0;
		int str_len = 0, cur_descr_width = first ? max_x - cur_par_width : descr_width;

		while(*p == ' ')
			p++;

		str_len = strlen(p);
		if (!str_len)
			break;

		len_before_ww = min(str_len, cur_descr_width);

		n = &p[len_before_ww];
		kn = n;

		if (str_len > cur_descr_width)
		{ 
			while (*n != ' ' && n_len < max_wrap_width)
			{
				n--;
				n_len++;
			}

			if (n_len >= max_wrap_width)
				n = kn;
		}

		len_after_ww = (int)(n - p);
		if (len_after_ww <= 0)
			break;

		copy = (char *)malloc(len_after_ww + 1);
		memcpy(copy, p, len_after_ww);
		copy[len_after_ww] = 0x00;

		if (first)
			first = 0;
		else
			fprintf(stderr, "%*s ", par_width, "");

		fprintf(stderr, "%s\n", copy);

		free(copy);

		p = n;
	}
}

std::vector<uint8_t> get_state(const int fd)
{
	std::vector<uint8_t> out;

	uint8_t cmd = '?';
	if (write(fd, &cmd, 1) != 1)
		error_exit(true, "Problem sending command to powerbank");

	// FIXME timeout of 100ms
	uint8_t buffer[51];
	if (read(fd, buffer, sizeof(buffer)) != sizeof(buffer))
		error_exit(true, "Problem receiving state from powerbank");

	for(unsigned i=0; i<sizeof(buffer); i++)
		out.push_back(buffer[i]);

	return out;
}

// celsius
double get_temp(const std::vector<uint8_t> & state)
{
	int16_t v = state.at(0) | (state.at(1) << 8);

	return v / 10.0;
}

// mV
double get_battery_voltage(const std::vector<uint8_t> & state)
{
	int16_t v = state.at(2) | (state.at(3) << 8);

	return v / 1000.0;
}

// mA
double get_charging_current(const std::vector<uint8_t> & state)
{
	int16_t v = state.at(4) | (state.at(5) << 8);

	return v / 1000.0;
}

// mA
double get_hv_output_current(const std::vector<uint8_t> & state)
{
	int16_t v = state.at(6) | (state.at(7) << 8);

	return v / 1000.0;
}

// mA
double get_usb_output_current(const std::vector<uint8_t> & state)
{
	int16_t v = state.at(8) | (state.at(9) << 8);

	return v / 1000.0;
}

// mV
double get_hv_output_voltage(const std::vector<uint8_t> & state)
{
	int16_t v = state.at(0x0a) | (state.at(0xb) << 8);

	return v / 1000.0;
}

std::vector<uint8_t> get_i2c_BQ24295(const std::vector<uint8_t> & state)
{
	std::vector<uint8_t> out;

	for(int i=0x18; i<=0x21; i++)
		out.push_back(state.at(i));

	return out;
}

uint8_t get_flags(const std::vector<uint8_t> & state)
{
	return state.at(0x22);
}

bool get_auto_send_statemachine(const std::vector<uint8_t> & state)
{
	return get_flags(state) & 128;
}

bool get_virtual_serial_port_connected(const std::vector<uint8_t> & state)
{
	return get_flags(state) & 64;
}

bool get_charging_port_plugged_in(const std::vector<uint8_t> & state)
{
	return get_flags(state) & 32;
}

bool get_warnings_enabled(const std::vector<uint8_t> & state)
{
	return get_flags(state) & 16;
}

bool get_charger_fault(const std::vector<uint8_t> & state)
{
	return get_flags(state) & 8;
}

bool get_battery_overvoltage(const std::vector<uint8_t> & state)
{
	return get_flags(state) & 4;
}

bool get_battery_too_cold(const std::vector<uint8_t> & state)
{
	return get_flags(state) & 2;
}

bool get_battery_too_hot(const std::vector<uint8_t> & state)
{
	return get_flags(state) & 1;
}

void json_double(const char *name, const double v, const bool next)
{
	printf("\"%s\" : %f", name, v);

	if (next)
		printf(",\n");
	else
		printf("\n");
}

void json_bool(const char *name, const bool v, const bool next)
{
	printf("\"%s\" : %s", name, v ? "true" : "false");

	if (next)
		printf(",\n");
	else
		printf("\n");
}

void dump(const int fd, const bool json)
{
	const std::vector<uint8_t> state = get_state(fd);

	if (json) {
		printf("{\n");
		json_double("battery-voltage", get_battery_voltage(state), true);
		json_double("charging-current", get_charging_current(state), true);
		json_double("HV-output-current", get_hv_output_current(state), true);
		json_double("HV-output-voltage", get_hv_output_voltage(state), true);
		json_double("USB-output-current", get_usb_output_current(state), true);

		json_bool("battery-overvoltage", get_battery_overvoltage(state), true);
		json_bool("auto-send-statemachine", get_auto_send_statemachine(state), true);
		json_bool("virtual-serial-port-connected", get_virtual_serial_port_connected(state), true);
		json_bool("charging-port-pluggend-in", get_charging_port_plugged_in(state), true);
		json_bool("warnings-enabled", get_warnings_enabled(state), true);
		json_bool("charger-fault", get_charger_fault(state), true);
		json_bool("battery-too-cold", get_battery_too_cold(state), true);
		json_bool("battery-too-hot", get_battery_too_hot(state), false);
		printf("}\n");
	}
	else {
		printf("temperature:\t%f\n", get_temp(state));
		printf("battery voltage:\t%f\n", get_battery_voltage(state));
		printf("charging current:\t%f\n", get_charging_current(state));
		printf("HV output current:\t%f\n", get_hv_output_current(state));
		printf("HV output voltage:\t%f\n", get_hv_output_voltage(state));
		printf("USB output current:\t%f\n", get_usb_output_current(state));

		if (get_battery_overvoltage(state))
			printf("Battery overvoltage!!\n");
		if (get_auto_send_statemachine(state))
			printf("Statemachine is in auto send mode\n");
		if (get_virtual_serial_port_connected(state))
			printf("Virtual serial port connected\n");
		if (get_charging_port_plugged_in(state))
			printf("Charging port plugged in\n");
		if (get_warnings_enabled(state))
			printf("Warnings enabled\n");
		if (get_charger_fault(state))
			printf("Charger fault\n");

		if (get_battery_too_cold(state))
			printf("Battery too cold!\n");
		else if (get_battery_too_hot(state))
			printf("Battery too hot!!!\n");
	}
}

void exec(const char *script)
{
	if (script)
		system(script);
}

void ups(const int fd, const unsigned power_off_after, const char *poweroff_script)
{
	for(;;) {
		std::vector<uint8_t> state = get_state(fd);

		if (get_charging_port_plugged_in(state) == false) {
			sleep(power_off_after);

			state = get_state(fd);
			if (get_charging_port_plugged_in(state) == false)
				exec(poweroff_script);
		}
	}
}

void version()
{
	fprintf(stderr, "powerbankcontrol v" VERSION " is (C) 2017 by folkert@vanheusden.com\n");
	fprintf(stderr, "PowerBank is (C) muxtronics.nl\n\n");
}

void help(void)
{
	fprintf(stderr, "\n");

	/* where to connect to */
	fprintf(stderr, gettext(" *** powerbankcontrol ***\n"));
	format_help("-d x", "--device", gettext("(virtual in case of USB -)serial device to which the powerbank is connected"));
	format_help("-f", "--fork", gettext("fork into the background (become daemon)"));
	format_help("-m", "--mode", gettext("mode of this tool: ups, dump"));
	format_help("-D", "--power-off-after", gettext("how long to wait before shutdown after power loss"));
	format_help("-s", "--shutdown-command", gettext("command to use to power down system (see -D and -m ups)"));
	format_help("-j", "--json", gettext("JSON output for -m dump"));
}

typedef enum { M_UPS, M_DUMP } pbc_mode_t;

int main(int argc, char *argv[])
{
	bool do_fork = false, json = false;
	const char *dev = "/dev/ttyUSB0";
	pbc_mode_t m = M_DUMP;
	unsigned power_off_after = 60;
	const char *poweroff_script = "/sbin/poweroff";

	determine_terminal_size();

	static struct option long_options[] =
	{
		{"device",   1, NULL, 'd' },
		{"fork",	0, NULL, 'f' },
		{"mode",	0, NULL, 'm' },
		{"power-off-after",   1, NULL, 'D' },
		{"shutdown-command",   1, NULL, 's' },
		{"json",   0, NULL, 'j' },
		{"version",	0, NULL, 'V' },
		{"help",	0, NULL, 'h' },
		{NULL,		0, NULL, 0   }
	};

	int c = -1;
	while((c = getopt_long(argc, argv, "d:fm:D:s:jVh", long_options, NULL)) != -1)
	{
		switch(c) {
			case 'd':
				dev = optarg;
				break;

			case 'f':
				do_fork = true;
				break;

			case 'm':
				if (strcasecmp(optarg, "dump") == 0)
					m = M_DUMP;
				else if (strcasecmp(optarg, "ups") == 0)
					m = M_UPS;
				else
					error_exit(false, "%s is an unknown mode", optarg);
				break;

			case 'D':
				power_off_after = atoi(optarg);
				break;

			case 's':
				poweroff_script = optarg;
				break;

			case 'j':
				json = true;
				break;

			case 'V':
				version();
				return 0;

			case 'h':
				help();
				return 0;

			default:
				help();
				return 1;
		}
	}

	int fd = open(dev, O_RDWR);
	if (fd == -1)
		error_exit(true, "Failed opening %s", dev);

	if (do_fork && daemon(0, 0) == -1)
		error_exit(true, "Failed forking into the background");

	setser(fd);

	autosend(fd, false); // ! we use polling

	if (m == M_DUMP)
		dump(fd, json);
	else
		ups(fd, power_off_after, poweroff_script);

	return 0;
}