#include <algorithm>
#include <fcntl.h>
#include <getopt.h>
#include <libintl.h>
#include <locale.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <vector>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "error.h"

// only required when using a real serial port
void setser(int fd) 
{
	struct termios newtio;

	if (tcgetattr(fd, &newtio) == -1)
		error_exit(true, gettext("tcgetattr failed: did you select a powerbank serial port?"));

	newtio.c_iflag = IGNBRK; // | ISTRIP;
	newtio.c_oflag = 0;
	newtio.c_cflag = B9600 | CS8 | CREAD | CLOCAL | CSTOPB;
	newtio.c_lflag = 0;
	newtio.c_cc[VMIN] = 1;
	newtio.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSANOW, &newtio) == -1)
		error_exit(true, gettext("tcsetattr failed: problem talking to serial port"));

	tcflush(fd, TCIOFLUSH);
}

bool ansi_terminal(void)
{
	const char *term = getenv("TERM");

	if (!isatty(1) || !isatty(2))
		return false;

	if (!term)
		return false;

	if (strcasestr(term, "ansi") == 0)
		return true;

	if (strcasestr(term, "console") == 0 || strcasestr(term, "con80x25") == 0 || strcasestr(term, "linux") == 0)
		return true;

	if (strcasestr(term, "screen") == 0)
		return true;

	if (strcasestr(term, "xterm") == 0)
		return true;

	if (strcasestr(term, "rxvt") == 0 || strcasestr(term, "konsole") == 0)
		return true;

	return false;
}

void set_bold(bool on)
{
	if (ansi_terminal()) {
		if (on)
			fprintf(stderr, "\x1b[1m");
		else
			fprintf(stderr, "\x1b[22m");
	}
}

void set_underline(bool on)
{
	if (ansi_terminal()) {
		if (on)
			fprintf(stderr, "\x1b[4m");
		else
			fprintf(stderr, "\x1b[24m");
	}
}

void reset_term()
{
	if (ansi_terminal())
		fprintf(stderr, "\x1b[0m\x1b[2K\r");
}

void help_header(const char *str)
{
	fprintf(stderr, "\n");

	set_bold(1);
	fprintf(stderr, " *** ");

	set_underline(1);
	fprintf(stderr, "%s", str);
	set_underline(0);

	fprintf(stderr, " ***\n");
	set_bold(0);
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
	else if (short_str)
		str_add(&line, "%s", short_str);
	else
		line = strdup("");

	cur_par_width = fprintf(stderr, "%-*s ", par_width, line);

	free(line);

	if (par_width + 1 >= max_x || cur_par_width >= max_x) {
		fprintf(stderr, "%s\n", descr);
		return;
	}

	for(;strlen(p);) {
		char *n =  NULL, *kn = NULL, *copy = NULL;
		int len_after_ww = 0, len_before_ww = 0;
		int str_len = 0, cur_descr_width = first ? max_x - cur_par_width : descr_width;

		while(*p == ' ')
			p++;

		str_len = strlen(p);
		if (!str_len)
			break;

		len_before_ww = std::min(str_len, cur_descr_width);

		n = &p[len_before_ww];
		kn = n;

		if (str_len > cur_descr_width) { 
			int n_len = 0;

			while (*n != ' ' && n_len < max_wrap_width) {
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

std::vector<uint8_t> get_bytes(const int fd, const unsigned n)
{
	std::vector<uint8_t> out;

	struct pollfd fds[1] = { { fd, POLLIN, 0 } };

	for(unsigned i=0; i<n; i++) {
		fds[0].revents = 0;

		int rc = poll(fds, 1, 100); // 100ms timeout
		if (rc == -1)
			error_exit(true, gettext("Poll on powerbank failed"));
		if (rc == 0)
			error_exit(true, gettext("Powerbank went silent"));

		uint8_t c = 0;
		rc = read(fd, &c, 1);
		if (rc <= 0)
			error_exit(true, gettext("Problem receiving state from powerbank"));

		out.push_back(c);
	}

	return out;
}

void request(const int fd, const uint8_t cmd)
{
	if (write(fd, &cmd, 1) != 1)
		error_exit(true, gettext("Problem sending command to powerbank"));
}

std::vector<uint8_t> get_state(const int fd)
{
	std::vector<uint8_t> out;

retry:
	request(fd, 0x70);

	uint8_t buffer[51], *p = buffer;

	struct pollfd fds[1] = { { fd, POLLIN, 0 } };

	int todo = sizeof buffer;
	for(; todo;) {
		fds[0].revents = 0;

		int rc = poll(fds, 1, 100); // 100ms timeout
		if (rc == -1)
			error_exit(true, gettext("Poll on powerbank failed"));
		if (rc == 0)
			goto retry;

		rc = read(fd, p, todo);
		if (rc <= 0)
			error_exit(true, gettext("Problem receiving state from powerbank"));

		p += rc;
		todo -= rc;
	}

	for(unsigned i=0; i<sizeof(buffer); i++)
		out.push_back(buffer[i]);

	return out;
}

// celsius
double get_temp(const std::vector<uint8_t> & state)
{
	int16_t v = state.at(0) | (state.at(1) << 8);

	return v / 100.0;
}

double get_milli(const std::vector<uint8_t> & state, const int offset)
{
	int16_t v = state.at(offset + 0) | (state.at(offset + 1) << 8);

	return v / 1000.0;
}

// mV
double get_battery_voltage(const std::vector<uint8_t> & state)
{
	return get_milli(state, 2);
}

// mA
double get_charging_current(const std::vector<uint8_t> & state)
{
	return get_milli(state, 4);
}

// mA
double get_hv_output_current(const std::vector<uint8_t> & state)
{
	return get_milli(state, 6);
}

// mA
double get_usb_output_current(const std::vector<uint8_t> & state)
{
	return get_milli(state, 8);
}

// mV
double get_hv_output_voltage(const std::vector<uint8_t> & state)
{
	return get_milli(state, 0x0a);
}

std::vector<uint8_t> get_i2c_BQ24295(const std::vector<uint8_t> & state)
{
	std::vector<uint8_t> out;

	for(int i=0x18; i<=0x21; i++)
		out.push_back(state.at(i));

	return out;
}

uint8_t get_flags_0x22(const std::vector<uint8_t> & state)
{
	return state.at(0x22);
}

bool get_auto_send_statemachine(const std::vector<uint8_t> & state)
{
	return get_flags_0x22(state) & 128;
}

bool get_virtual_serial_port_connected(const std::vector<uint8_t> & state)
{
	return get_flags_0x22(state) & 64;
}

bool get_charging_port_plugged_in(const std::vector<uint8_t> & state)
{
	return get_flags_0x22(state) & 32;
}

bool get_warnings_enabled(const std::vector<uint8_t> & state)
{
	return get_flags_0x22(state) & 16;
}

bool get_charger_fault(const std::vector<uint8_t> & state)
{
	return get_flags_0x22(state) & 8;
}

bool get_battery_overvoltage(const std::vector<uint8_t> & state)
{
	return get_flags_0x22(state) & 4;
}

bool get_battery_too_cold(const std::vector<uint8_t> & state)
{
	return get_flags_0x22(state) & 2;
}

bool get_battery_too_hot(const std::vector<uint8_t> & state)
{
	return get_flags_0x22(state) & 1;
}

uint8_t get_flags_0x23(const std::vector<uint8_t> & state)
{
	return state.at(0x23);
}

//
bool get_hv_output_on(const std::vector<uint8_t> & state)
{
	return get_flags_0x23(state) & 128;
}

bool get_usb_output_on(const std::vector<uint8_t> & state)
{
	return get_flags_0x23(state) & 64;
}

uint32_t get_battery_uptime(const std::vector<uint8_t> & state)
{
	return (state.at(0x27) << 24) | (state.at(0x26) << 16) | (state.at(0x25) << 8) | state.at(0x24);
}

void json_double(const char *name, const double v, const bool next)
{
	printf("\"%s\" : %f", name, v);

	if (next)
		printf(",\n");
	else
		printf("\n");
}

void json_uint32_t(const char *name, const uint32_t v, const bool next)
{
	printf("\"%s\" : %u", name, v);

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

std::string to_string(const std::vector<uint8_t> & bytes, const unsigned n)
{
	std::string out;

	for(unsigned i=0; i<n; i++)
		out += char(bytes.at(i));

	return out;
}

std::string get_name(const int fd)
{
	request(fd, 0x42);

	std::vector<uint8_t> name_bytes = get_bytes(fd, 18);

	return to_string(name_bytes, 16);
}

std::string get_descr(const int fd)
{
	request(fd, 0xff);

	std::vector<uint8_t> descr_bytes = get_bytes(fd, 24);

	return to_string(descr_bytes, 24);
}

void inc_hv(const int fd)
{
	request(fd, 0x73);
}

void dec_hv(const int fd)
{
	request(fd, 0x74);
}

void set_hv(const int fd, const char *parameter)
{
	if (!parameter)
		error_exit(false, gettext("Parameter missing"));

	if (strcasecmp(parameter, "on") == 0)
		request(fd, 0x77);
	else
		request(fd, 0x78);
}

void set_usb(const int fd, const char *parameter)
{
	if (!parameter)
		error_exit(false, gettext("Parameter missing"));

	if (strcasecmp(parameter, "on") == 0)
		request(fd, 0x75);
	else
		request(fd, 0x76);
}

void set_name(const int fd, const char *const name)
{
	char temp[17];
	memset(temp, 0x00, sizeof(temp));

	if (name) {
		size_t l = strlen(name);

		if (l > 16)
			error_exit(false, gettext("Name too long"));

		memcpy(temp, name, l);
	}

	request(fd, 0x43);

	if (write(fd, temp, 16) != 16)
		error_exit(true, gettext("Error talking to power bank"));
}

char to_hex(const int v)
{
	if (v <= 9)
		return '0' + v;

	return 'a' + v - 10;
}

void set_bq24295(const int fd, const int idx, const char *parameter)
{
	if (!parameter)
		error_exit(false, gettext("Parameter missing"));

	if (idx < 0 || idx > 9)
		error_exit(false, gettext("Index out of range"));

	unsigned p = atoi(parameter);

	char cmd[4] = { 0x71, char('0' + idx), to_hex(p >> 4), to_hex(p & 15) };

	if (write(fd, cmd, sizeof cmd) != sizeof cmd)
		error_exit(true, gettext("Error talking to power bank"));
}

void json_string(const char *name, const std::string & v, const bool next)
{
	printf("\"%s\" : \"%s\"", name, v.c_str());

	if (next)
		printf(",\n");
	else
		printf("\n");
}

void dump(const int fd, const bool json)
{
	const std::vector<uint8_t> state = get_state(fd);

	std::string name = get_name(fd);

	std::string descr = get_descr(fd);

	if (json) {
		printf("{\n");
		json_string("name", name, true);
		json_string("descr", descr, true);

		json_double("battery-voltage", get_battery_voltage(state), true);
		json_double("charging-current", get_charging_current(state), true);
		json_double("HV-output-current", get_hv_output_current(state), true);
		json_double("HV-output-voltage", get_hv_output_voltage(state), true);
		json_double("USB-output-current", get_usb_output_current(state), true);
		json_uint32_t("battery-uptime", get_battery_uptime(state), true);

		std::vector<uint8_t> c = get_i2c_BQ24295(state);
		for(unsigned i=0; i<unsigned(c.size()); i++) {
			char *buffer = NULL;
			asprintf(&buffer, "bq24295-reg-%u", i);

			json_uint32_t(buffer, c.at(i), true);

			free(buffer);
		}

		json_bool("battery-overvoltage", get_battery_overvoltage(state), true);
		json_bool("auto-send-statemachine", get_auto_send_statemachine(state), true);
		json_bool("virtual-serial-port-connected", get_virtual_serial_port_connected(state), true);
		json_bool("charging-port-pluggend-in", get_charging_port_plugged_in(state), true);
		json_bool("warnings-enabled", get_warnings_enabled(state), true);
		json_bool("charger-fault", get_charger_fault(state), true);
		json_bool("battery-too-cold", get_battery_too_cold(state), true);
		json_bool("battery-too-hot", get_battery_too_hot(state), true);
		json_bool("hv-output", get_hv_output_on(state), true);
		json_bool("usb-output", get_usb_output_on(state), false);
		printf("}\n");
	}
	else {
		printf(gettext("name:\t%s\n"), name.c_str());

		printf(gettext("temperature:\t%f degreese celsius\n"), get_temp(state));
		printf(gettext("battery voltage:\t%f V\n"), get_battery_voltage(state));
		printf(gettext("charging current:\t%f A\n"), get_charging_current(state));
		printf(gettext("HV output current:\t%f A\n"), get_hv_output_current(state));
		printf(gettext("HV output voltage:\t%f V\n"), get_hv_output_voltage(state));
		printf(gettext("USB output current:\t%f A\n"), get_usb_output_current(state));
		printf(gettext("Battery uptime:\t%u seconds\n"), get_battery_uptime(state));

		printf(gettext("BQ24295 registers:\t"));
		std::vector<uint8_t> c = get_i2c_BQ24295(state);
		for(size_t i=0; i<c.size(); i++) {
			if (i)
				printf(" ");

			printf("%02x", c.at(i));
		}
		printf("\n");

		if (get_battery_overvoltage(state))
			printf(gettext("Battery overvoltage!!\n"));
		if (get_auto_send_statemachine(state))
			printf(gettext("Statemachine is in auto send mode\n"));
		if (get_virtual_serial_port_connected(state))
			printf(gettext("Virtual serial port connected\n"));
		if (get_charging_port_plugged_in(state))
			printf(gettext("Charging port plugged in\n"));
		if (get_warnings_enabled(state))
			printf(gettext("Warnings enabled\n"));
		if (get_charger_fault(state))
			printf(gettext("Charger fault\n"));
		if (get_battery_too_cold(state))
			printf(gettext("Battery too cold!\n"));
		if (get_battery_too_hot(state))
			printf(gettext("Battery too hot!!!\n"));
		if (get_hv_output_on(state))
			printf(gettext("HV output on\n"));
		if (get_usb_output_on(state))
			printf(gettext("USB output on\n"));
	}
}

void putChar(char *line, int x, const char *c, const bool red)
{
	if (x < 0)
		x = 0;

	if (x >= max_x)
		x = max_x -1;

	memcpy(&line[x], c, strlen(c));

	if (ansi_terminal()) {
		fprintf(stderr, "\x1b[%dG", x + 1);

		if (red)
			fprintf(stderr, "\x1b[31m");
		else
			fprintf(stderr, "\x1b[32m");

		fprintf(stderr, "%s", c);
	}
}

void graph(const int fd, const char *parameter)
{
	bool first = true;
	int y = 0;

	double scale_voltage = (max_x - 1) / 24.0, scale_current = (max_x - 1) / 3.0;
	char line[max_x];

	int interval = parameter ? atoi(parameter) : 200;

	for(;;) {
		if (++y >= max_y - 3 || first) {
			printf(gettext("| battery voltage, * charging current, + hv output current,\n"));
			printf(gettext("- hv output voltage, # usb output current\n"));

			memset(line, ' ', sizeof line);
			line[max_x - 1] = 0x00;

			putChar(line, int(scale_current * 1.0), "C1", true);
			putChar(line, int(scale_current * 2.0), "C2", true);

			putChar(line, int(scale_voltage * 3.0), "V3", false);
			putChar(line, int(scale_voltage * 5.0), "V5", false);
			putChar(line, int(scale_voltage * 10.0), "V10", false);
			putChar(line, int(scale_voltage * 15.0), "V15", false);
			putChar(line, int(scale_voltage * 20.0), "V20", false);

			if (!ansi_terminal())
				printf("%s\n", line);

			y = 0;
			first = false;
		}

		const std::vector<uint8_t> state = get_state(fd);

		double battery_voltage = get_battery_voltage(state);
		double charging_current = get_charging_current(state);
		double hv_output_current = get_hv_output_current(state);
		double hv_output_voltage = get_hv_output_voltage(state);
		double usb_output_current = get_usb_output_current(state);

		memset(line, ' ', sizeof line);
		line[max_x - 1] = 0x00;

		int x;

		x = battery_voltage * scale_voltage;
		putChar(line, x, "|", false);

		x = charging_current * scale_current;
		putChar(line, x, "*", true);

		x = hv_output_current * scale_current;
		putChar(line, x, "+", true);

		x = hv_output_voltage * scale_voltage;
		putChar(line, x, "-", false);

		x = usb_output_current * scale_current;
		putChar(line, x, "#", true);

		if (ansi_terminal())
			fprintf(stderr, "\x1b[m\n");
		else
			printf("%s\n", line);

		usleep(interval * 1000);
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
	help_header(gettext("main"));
	format_help("-d x", "--device", gettext("(virtual in case of USB -)serial device to which the powerbank is connected"));
	format_help("-f", "--fork", gettext("fork into the background (become daemon)"));
	format_help("-m", "--mode", gettext("mode of this tool: ups, dump, set-name, set-bq24295, set-usb, set-hv"));
	format_help(NULL, NULL, gettext("- ups: shutdown system when power is off for a while (-D) using a user selected command (-s)"));
	format_help(NULL, NULL, gettext("- graph: draw a graph (on the terminal) in realtime of all measurements. use -p to set an interval in ms."));
	format_help(NULL, NULL, gettext("- dump: dump configuration & state of power bank"));
	format_help(NULL, NULL, gettext("- set-name: configure name of bank"));
	format_help(NULL, NULL, gettext("- set-bq24295: configure charger chip, see data-sheet at http://www.ti.com/lit/ds/symlink/bq24295.pdf"));
	format_help(NULL, NULL, gettext("- set-usb: toggle state of USB power (-p: on/off)"));
	format_help(NULL, NULL, gettext("- set-hv: toggle state of HV power (-p: on/off)"));
	format_help(NULL, NULL, gettext("- inc-hv: increase HV voltage (in 64 steps)"));
	format_help(NULL, NULL, gettext("- dec-hv: decrease HV voltage (in 64 steps)"));
	format_help("-p", "--parameter", gettext("parameter (if any) for the command chosen"));

	help_header(gettext("configuring bq24295"));
	format_help("-i", "--index", gettext("index (if any) for the command chosen"));

	help_header(gettext("ups mode"));
	format_help("-D", "--power-off-after", gettext("how long to wait before shutdown after power loss"));
	format_help("-s", "--shutdown-command", gettext("command to use to power down system (see -D and -m ups)"));

	help_header(gettext("dump format"));
	format_help("-j", "--json", gettext("JSON output for -m dump"));

	help_header(gettext("meta"));
	format_help("-v", "--version", gettext("get version of this program"));
	format_help("-h", "--help", gettext("get this help"));
}

typedef enum { M_UPS, M_DUMP, M_GRAPH, M_SET_NAME, M_SET_bq24295, M_SET_USB, M_SET_HV, M_INC_HV, M_DEC_HV } pbc_mode_t;

int main(int argc, char *argv[])
{
	bool do_fork = false, json = false;
	const char *dev = "/dev/ttyACM0";
	pbc_mode_t m = M_DUMP;
	unsigned power_off_after = 60;
	const char *poweroff_script = "/sbin/poweroff";
	const char *parameter = NULL;
	int idx = -1;

	determine_terminal_size();

	setlocale(LC_ALL, "");
	bindtextdomain("powerbankcontrol", "/usr/share/locale");
	textdomain("powerbankcontrol");

	static struct option long_options[] =
	{
		{"device",   	1, NULL, 'd' },
		{"fork",	0, NULL, 'f' },
		{"mode",	0, NULL, 'm' },
		{"power-off-after",	1, NULL, 'D' },
		{"shutdown-command",	1, NULL, 's' },
		{"json",   	0, NULL, 'j' },
		{"parameter",  	0, NULL, 'p' },
		{"index",  	0, NULL, 'i' },
		{"version",	0, NULL, 'V' },
		{"help",	0, NULL, 'h' },
		{NULL,		0, NULL, 0   }
	};

	int c = -1;
	while((c = getopt_long(argc, argv, "d:fm:D:s:jp:i:Vh", long_options, NULL)) != -1)
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
				else if (strcasecmp(optarg, "graph") == 0)
					m = M_GRAPH;
				else if (strcasecmp(optarg, "ups") == 0)
					m = M_UPS;
				else if (strcasecmp(optarg, "set-name") == 0)
					m = M_SET_NAME;
				else if (strcasecmp(optarg, "set-bq24295") == 0)
					m = M_SET_bq24295;
				else if (strcasecmp(optarg, "set-usb") == 0)
					m = M_SET_USB;
				else if (strcasecmp(optarg, "set-hv") == 0)
					m = M_SET_HV;
				else if (strcasecmp(optarg, "inc-hv") == 0)
					m = M_INC_HV;
				else if (strcasecmp(optarg, "dec-hv") == 0)
					m = M_DEC_HV;
				else
					error_exit(false, gettext("%s is an unknown mode"), optarg);
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

			case 'p':
				parameter = optarg;
				break;

			case 'i':
				idx = atoi(optarg);
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
		error_exit(true, gettext("Failed opening %s"), dev);

	if (do_fork && daemon(0, 0) == -1)
		error_exit(true, gettext("Failed forking into the background"));

	setser(fd);

	if (m == M_DUMP)
		dump(fd, json);
	else if (m == M_GRAPH)
		graph(fd, parameter);
	else if (m == M_SET_NAME)
		set_name(fd, parameter);
	else if (m == M_SET_bq24295)
		set_bq24295(fd, idx, parameter);
	else if (m == M_SET_HV)
		set_hv(fd, parameter);
	else if (m == M_SET_USB)
		set_usb(fd, parameter);
	else if (m == M_INC_HV)
		inc_hv(fd);
	else if (m == M_DEC_HV)
		dec_hv(fd);
	else if (m == M_UPS)
		ups(fd, power_off_after, poweroff_script);

	return 0;
}
