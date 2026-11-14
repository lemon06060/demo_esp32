#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <mutex>
#include <sstream>

class Logger
{
	public:
		enum Level{
			INFO,
			WARN,
			ERROR
		};

	Logger(const std::string& filename = "gateway.log") 
	{
		m_file.open(filename, std::ios::app);
	}

	~Logger()
	{
		if (m_file.is_open()) {
			m_file.close();
		}
	}

	void log(Level level, const std::string& message) {
		std::lock_guard<std::mutex> lock(m_mutex);

		std::string level_str;
		switch(level){
			case INFO:	level_str = "[INFO] ";	break;
			case WARN:	level_str = "[WARN] ";	break;
			case ERROR:	level_str = "[ERROR] ";	break;
		}

		std::string timestamp = get_timestamp();
		std::string full_msg = timestamp + " " + level_str + message;

		// 输出到终端
		std::cout << full_msg << std::endl;

		// 输出到文件
		if (m_file.is_open()) {
			m_file << full_msg << std::endl;
			m_file.flush();
		}
	}

    void info(const std::string& msg) {  log(Logger::INFO, msg);    }
    void warn(const std::string& msg) {  log(Logger::WARN, msg);    }
    void error(const std::string& msg) {  log(Logger::ERROR, msg);    }

	private:
		std::string get_timestamp() {
			time_t now = time(nullptr);
			struct tm* tm_info = localtime(&now);
			char buffer[20];
			strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
			return std::string(buffer);
		}

		std::ofstream m_file;
		std::mutex m_mutex;
};
