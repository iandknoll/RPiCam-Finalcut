// CAMERA DEPENDENCIES
#include "core/rpicam_encoder.hpp"
#include "core/logging.hpp"
#include "output/output.hpp"
#include <functional>
#include <memory>

// FRONT END DEPENDENCIES
#include <final/final.h>
#include <ctime>
#include <chrono>
#include <filesystem>

// SHARED DEPENDENCIES
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <stdexcept>


// GLOBALS
enum class StopType {
	USER = 0,
	TIMEOUT = 1,
	ERROR = -1
};

struct CameraStopInfo {
	StopType type = StopType::ERROR;
	std::string error_message = "";
};

static std::atomic<bool> stop_camera{false};
static std::atomic<bool> camera_finished{false};

static std::mutex camera_stop_info_mutex;

static CameraStopInfo camera_stop_info;

// CAMERA
static int get_colourspace_flags(std::string const& codec) {

	if (codec == "mjpeg" || codec == "yuv420") {
		return RPiCamEncoder::FLAG_VIDEO_JPEG_COLOURSPACE;
	}
	else {
		return RPiCamEncoder::FLAG_VIDEO_NONE;
	}
}

void VidStart(std::string const& name) {
	bool EncoderOn{false};
	bool CameraOn{false};

	RPiCamEncoder app;

	auto TryEncoderOff = [&app, &EncoderOn]() {
		try {
			if (EncoderOn) {
				app.StopEncoder();
				EncoderOn = false;
			}
			return true;
		} catch (std::exception const &e) {
			LOG_ERROR("ERROR: Unable to stop encoder: " << e.what());
			return false;
		}
	};

	auto TryCameraOff = [&app, &CameraOn]() {
		try {
			if (CameraOn) {
				app.StopCamera();
				CameraOn = false;
			}
			return true;
		} catch (std::exception const &e) {
			LOG_ERROR("ERROR: Unable to stop camera:"  << e.what());
			return false;
		}
	};

	try
	{
		VideoOptions *options = app.GetOptions();

		// Build argv array w/ options
		// Adjust these values according to your own needs:
		std::vector<std::string> args = {
			"program",								// argv format requires a program name as first entry
			"--output", name,						// file name
			"--timeout", "40min",					// MAX recording time
			"--codec", "mjpeg",						// codec for video encoding/decoding
			"--profile", "baseline",				// Compression profile
			"--framerate", "240",					// fps goal
			"--viewfinder-width", "800",			// frame width (in pixels) (ISP level)
			"--viewfinder-height", "800",			// frame height (in pixels) (ISP level)
			"--width", "800",						// frame width (in pixels) (encoder level)
			"--height", "800",						// frame height (in pixels) (encoder level)
			"--awbgains", "2,2",					// disable auto white balance
			"--shutter", "3000us",					// Shutter speed (us)
			"--gain", "2",							// analog gain
			"--denoise", "cdn_off",					// turn off color denoise (for fps)
			"--nopreview"							// turn off preview (for fps)
		};

		std::vector<char*> argv;
		// Declare a vector array for type char*
		for (auto& arg: args) {
			argv.push_back(const_cast<char*>(arg.c_str()));
		}
		// Parse() method expects a char* array
		// To get this, go through each entry in the args string array:
		// For each entry, we first convert to C-style const char* w/ c_str()
		// Then remove the const qualifier with const_cast<char*>
		// Finally, store as next entry in our vector array w/ .push_back()

		if (!options->Parse(argv.size(), argv.data())) {
			throw std::runtime_error("Failed to parse options");
		}

		std::unique_ptr<Output> output = std::unique_ptr<Output>(Output::Create(options));

		app.SetEncodeOutputReadyCallback(std::bind(&Output::OutputReady, output.get(),
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3,
			std::placeholders::_4));

		app.SetMetadataReadyCallback(std::bind(&Output::MetadataReady, output.get(),
			std::placeholders::_1));

		app.OpenCamera();

		auto start_time = std::chrono::high_resolution_clock::now();

		app.ConfigureVideo(get_colourspace_flags(options->Get().codec));

		app.StartEncoder();
		EncoderOn = true;

		app.StartCamera();
		CameraOn = true;

		for (;;)
		{
			RPiCamEncoder::Msg msg = app.Wait();

			if (msg.type == RPiCamApp::MsgType::Quit)
			{
				if (!TryCameraOff() || !TryEncoderOff()) {
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					camera_stop_info = CameraStopInfo{StopType::ERROR, "Failed to stop camera/encoder"};
				} else {
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					camera_stop_info = CameraStopInfo{StopType::USER, ""};
				}
				camera_finished.store(true);
				return;
			}
			else if (msg.type == RPiCamApp::MsgType::Timeout)
			{
				LOG_ERROR("ERROR: Device timeout detected, attempting restart!");
				try {
					app.StopCamera();
					CameraOn = false;
					app.StartCamera();
					CameraOn = true;
				}
				catch (std::exception const &e) {
					if (!TryCameraOff() || !TryEncoderOff()) {
						std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
						camera_stop_info = CameraStopInfo{StopType::ERROR, "Failed to stop camera/encoder"};
					} else {
						std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
						camera_stop_info = CameraStopInfo{
							StopType::ERROR,
							std::string("Camera restart failed: ") + e.what()
						};
					}
					camera_finished.store(true);
					return;
				}
				continue;
			}
			else if (msg.type != RPiCamApp::MsgType::RequestComplete)
			{
				if (!TryCameraOff() || !TryEncoderOff()) {
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					camera_stop_info = CameraStopInfo{StopType::ERROR, "Failed to stop camera/encoder"};
				} else {
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					camera_stop_info = CameraStopInfo{
						StopType::ERROR,
						"Unexpected message type received"
					};
				}
				camera_finished.store(true);
				return;
			}

			auto now = std::chrono::high_resolution_clock::now();
			if ((now - start_time) > options->Get().timeout.value)
			{
				if (!TryCameraOff() || !TryEncoderOff()) {
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					camera_stop_info = CameraStopInfo{StopType::ERROR, "Failed to stop camera/encoder"};
				} else {
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					camera_stop_info = CameraStopInfo{
						StopType::TIMEOUT,
						""
					};
				}
				camera_finished.store(true);
				return;
			}

			if (stop_camera.load())
			{
				if (!TryCameraOff() || !TryEncoderOff()) {
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					camera_stop_info = CameraStopInfo{StopType::ERROR, "Failed to stop camera/encoder"};
				} else {
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					camera_stop_info = CameraStopInfo{
						StopType::USER,
						""
					};
				}
				camera_finished.store(true);
				return;
			}
			
			CompletedRequestPtr &completed_request = std::get<CompletedRequestPtr>(msg.payload);
			
			app.EncodeBuffer(completed_request, app.VideoStream());
  		}
	}
	catch (std::exception const &e)
	{
		if (!TryCameraOff() || !TryEncoderOff()) {
			std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
			camera_stop_info = CameraStopInfo{StopType::ERROR, "Failed to stop camera/encoder"};
		} else {
			std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
			camera_stop_info = CameraStopInfo{
				StopType::ERROR,
				std::string("Exception: ") + e.what()
			};
		}
		camera_finished.store(true);
		return;
	}
}



// FRONT END UI
auto filename_time() -> std::string
{

	std::time_t timestamp = std::time(nullptr);
	
	std::tm datetime;
	
	localtime_r(&timestamp, &datetime);

	char output[80];

	std::strftime(output, 80, "%m-%d-%y_%H-%M-%S.mp4", &datetime);
	
	return output;
}

class ConfirmButton : public finalcut::FButton
{
	public:

		explicit ConfirmButton (finalcut::FWidget* parent = nullptr)
			 : finalcut::FButton{parent}
		{
			initLayout();
		}

	private:

		void initLayout()
		{
			setText ("Start Video");
			setGeometry(finalcut::FPoint{20,8}, finalcut::FSize{14,1});

			finalcut::FButton::initLayout();
		}
};

class YesButton : public finalcut::FButton
{

	public:

		explicit YesButton (finalcut::FWidget* parent = nullptr)
			 : finalcut::FButton{parent}
		{
			initLayout();
		}

	private:

		void initLayout()
		{

			setText ("Yes");
			setGeometry(finalcut::FPoint{20,8}, finalcut::FSize{4,1});

			finalcut::FButton::initLayout();
		}
};

class NoButton : public finalcut::FButton
{

	public:

		explicit NoButton (finalcut::FWidget* parent = nullptr)
			 : finalcut::FButton{parent}
		{
			initLayout();
		}

	private:

		void initLayout()
		{
			setText ("No");
			setGeometry(finalcut::FPoint{31,8}, finalcut::FSize{4,1});

			finalcut::FButton::initLayout();
		}
};

class FileName : public finalcut::FLineEdit
{

	public:

		explicit FileName (finalcut::FWidget* parent = nullptr)
			 : finalcut::FLineEdit{parent}
		{
			initLayout();
		}

	private:

		void initLayout()
		{
			setInputFilter("[a-zA-Z0-9 ._-]");
			setMaxLength(255);

			std::string fsuggestion{filename_time()};

			setText (finalcut::FString{fsuggestion});
			setGeometry (finalcut::FPoint{20,4}, finalcut::FSize{30,1});

			setLabelText("File Name: ");

			finalcut::FLineEdit::initLayout();
		}
};

class ErrLog : public finalcut::FTextView
{
	public:

		explicit ErrLog (finalcut::FWidget* parent = nullptr)
			: finalcut::FTextView{parent}
		{
			initLayout();
		}

	private:

		void initLayout()
		{
			setGeometry (finalcut::FPoint{3,13}, finalcut::FSize{50,3});
			setText("");
		}
};

class Stopwatch : public finalcut::FLabel
{

	public:

		explicit Stopwatch (finalcut::FWidget* parent = nullptr)
			 : finalcut::FLabel{parent}
		{
			initLayout();
		}
		
		void start()
		{
			if (!is_running)
			{
				start_time = std::chrono::steady_clock::now();
				
				timer_id = addTimer(1000);
				
				if (timer_id <= 0)
				{
					setText("ERROR: Timer failed to start");
				}
				else
				{
					is_running = true;
				}
			}
		}
		
		void stop()
		{
			if (is_running)
			{
				is_running = false;

				if (timer_id > 0)
				{
					delTimer(timer_id);
					setText("");
				}
			}
		}

	private:
		
		bool is_running{false};
		int timer_id{0};
		std::chrono::steady_clock::time_point start_time;

		void initLayout()
		{
			setText("");
			setGeometry(finalcut::FPoint{3,11}, finalcut::FSize{50,1});

			setAlignment(finalcut::Align::Center);
			
			finalcut::FLabel::initLayout();
		}
		
		void onTimer(finalcut::FTimerEvent* ev) override
		{

			if (ev->getTimerId() != timer_id)
			{
				return;
			}
			
			auto now = std::chrono::steady_clock::now();
			auto elapsed = now - start_time;
			auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
			int minutes{static_cast<int>(total_seconds.count() % 3600) / 60};
			int seconds{static_cast<int>(total_seconds.count() % 60)};
			
			finalcut::FString time_str;
			time_str.sprintf("Run Time: %02d:%02d", minutes, seconds);
			setText(time_str);
			
			redraw();		
		}
};

class MainDialog : public finalcut::FDialog
{

	public:

		explicit MainDialog (finalcut::FWidget* parent = nullptr)
			 : finalcut::FDialog{parent}
		{
			initLayout();
			initCallbacks();
			
			updateButtonVisibility();
		}

		~MainDialog()
		{

			if (camera_thread.joinable())
			{
				stop_camera.store(true);
				camera_thread.join();
			}
			
		}

	private:
		
		// Initialize child widgets (defined above):
		FileName input{this};
		ConfirmButton confirmbutton{this};
		YesButton yesbutton{this};
		NoButton nobutton{this};
		ErrLog errors{this};
		Stopwatch status{this};
		
		bool showYesNo{false};
		
		std::string std_filename{};
		
		std::thread camera_thread{};

		void checkMinValue (int& n)
		{
			if ( n < 1 ) {
				n = 1 ;
			}
		}

		void initLayout()
		{			
			setText ("Reaching Task Camera Control");

			auto x = int((getDesktopWidth() - 56) / 2);
			auto y = int((getDesktopHeight() - 15) / 2);
			checkMinValue(x);
			checkMinValue(y);
			
			setGeometry (finalcut::FPoint{x,y}, finalcut::FSize{56,18});	

			finalcut::FDialog::initLayout();
		}
		
		void initCallbacks()
		{
				confirmbutton.addCallback
				(
					"clicked",
					this,
					&MainDialog::cb_cbutton
				);
				
				yesbutton.addCallback
				(
					"clicked",
					this,
					&MainDialog::cb_ybutton
				);
				
				nobutton.addCallback
				(
					"clicked",
					this,
					&MainDialog::cb_nbutton
				);
		}
		
		void cb_cbutton()
		{
			if (confirmbutton.getText() == "Start Video")
			{
				auto filename = input.getText();
				
				std_filename = filename.toString();

				if (std_filename.empty())
				{					
					status.setText("ERROR: No file name given");
				}
				else if (std_filename.length() < 4 || std_filename.substr(std_filename.length() - 4) != ".mp4")
				{					
					status.setText("ERROR: File must have .mp4 extension");
				}
				else if (std::filesystem::exists(std_filename))
				{					
					status.setText("WARNING: File already exists. Overwrite?");
					
					showYesNo = true;
				}
				else
				{					
					StartProtocol();
				}
			}
			else
			{				
				StopProtocol();
			}
			
			updateButtonVisibility();
			updateScreen();
		}
		
		void cb_ybutton()
		{			
			showYesNo = false;

			StartProtocol();
			
			updateButtonVisibility();
			updateScreen();
		}
		
		void cb_nbutton()
		{			
			showYesNo = false;
			status.setText("");
			
			updateButtonVisibility();
			updateScreen();		
		}
		
		void StartProtocol()
		{
			if (camera_thread.joinable()) {				
				return;
			}

			try {
				errors.setText("");
				camera_thread = std::thread([this, filename = std_filename]()
				{
					try {
						VidStart(filename);
					} catch (std::exception const &e) {
						{
							std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
							camera_stop_info = CameraStopInfo{
								StopType::ERROR,
								std::string("Unexpected exception: ") + e.what()
							};
						}
						camera_finished.store(true);
					}
				});
			} catch (std::exception const &e) {
				status.setText(finalcut::FString("ERROR: Failed to initialize camera thread"));
				return;
			}
				
			status.start();
			
			confirmbutton.setText("Stop Video");
		}
		
		void StopProtocol()
		{
			if (camera_thread.joinable()) {
				stop_camera.store(true);	

				status.setText(finalcut::FString("Stopping..."));

				status.redraw();
			}
		}
		
		void updateButtonVisibility()
		{
			confirmbutton.setVisible(!showYesNo);
			yesbutton.setVisible(showYesNo);
			nobutton.setVisible(showYesNo);
		}
		
		void updateScreen()
		{
			redraw();
		}

		void onUserEvent(finalcut::FUserEvent* ev) override {
			
			const auto& info = ev ->getData<CameraStopInfo>();

			if (camera_thread.joinable()) {			
				camera_thread.join();				
				camera_thread = std::thread();
			}

			status.stop();

			switch (info.type)
			{
				case StopType::USER: // succesful operation
					errors.setText("");
					status.setText(
						finalcut::FString("Video saved as: ") + 
						finalcut::FString(std_filename)
						);
					break;
				case StopType::TIMEOUT: // timeout
					errors.setText("");
					status.setText(
						finalcut::FString("MAX DURATION. Video saved as: ") +
						finalcut::FString(std_filename)
						);
					break;
				case StopType::ERROR: // video error
					if (info.error_message.empty()) {
						errors.setText(finalcut::FString("ERROR: Recording failed"));
						// Recording failed, but no error message (give generic)
					} else {
						errors.setText(
							finalcut::FString("ERROR: ") +
							finalcut::FString(info.error_message)
							);
						// Recording failed, send message w/ error
					}
					break;
				default:
					errors.setText(finalcut::FString("ERROR: Unknown stop reason"));
					break;
			}

			input.setText (finalcut::FString {filename_time()});
			
			confirmbutton.setText("Start Video");
			
			stop_camera.store(false);

			redraw();			
		}
};

class CameraApplication : public finalcut::FApplication {

	public:
		CameraApplication(const int& argc, char* argv[]) 
			: finalcut::FApplication(argc, argv) { }

	private:

		void processExternalUserEvent() override {
			if (camera_finished.load() && getMainWidget()) {
				camera_finished.store(false);

				CameraStopInfo info;
				{
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					info = camera_stop_info;
				}

				finalcut::FUserEvent user_event(finalcut::Event::User, 0);

				user_event.setData(info);

				finalcut::FApplication::sendEvent(getMainWidget(), &user_event);
			}
		}
};

auto main (int argc, char* argv[]) -> int
{
	CameraApplication app{argc, argv};

	MainDialog dialog{&app};
	
	finalcut::FWidget::setMainWidget(&dialog);

	dialog.show();

	return app.exec();
}
