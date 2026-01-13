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
		} catch (std::exception const &e) {
			LOG_ERROR("ERROR: Unable to stop encoder: " << e.what());
			
			throw std::runtime_error("Failed to stop encoder: " +std::string(e.what()));
		}
	};

	auto TryCameraOff = [&app, &CameraOn]() {
		try {
			if (CameraOn) {
				app.StopCamera();
				CameraOn = false;
			}
		} catch (std::exception const &e) {
			LOG_ERROR("ERROR: Unable to stop camera:"  << e.what());
			
			throw std::runtime_error("Failed to stop camera: " +std::string(e.what()));
		}
	};
	
	try
	{
		VideoOptions *options = app.GetOptions();
		
		// Adjust these values according to your own needs:
		options->output = name; 				// file name (here our input)
		options->timeout.set("40min"); 			// MAX Recording time
		options->codec  = "h264";				// codec for video encoding/decoding
		options->profile = "baseline";  		// Compression profile
		options->framerate = 240;				// fps
		options->width = 800;					// frame width (in pixels)
		options->height = 800;					// frame height (in pixels)
		options->awbgains = "2,2";				// disable automatic white balance
		options->shutter.set("3000us");			// Shutter speed (us)
		options->gain = 2;						// analog gain
		options->denoise = "cdn_off"; 			// turns off color denoise (for fps)
		options->nopreview = true;				// turns off preview (for fps)

		std::unique_ptr<Output> output = std::unique_ptr<Output>(Output::Create(options));

		app.SetEncodeOutputReadyCallback(std::bind(&Output::OutputReady, output.get(), 
			std::placeholders::_1, 
			std::placeholders::_2, 
			std::placeholders::_3, 
			std::placeholders::_4));

		app.SetMetadataReadyCallback(std::bind(&Output::MetadataReady, output.get(), 
			std::placeholders::_1));
		
		app.OpenCamera();
		
		app.ConfigureVideo(get_colourspace_flags(options->codec));
		
		app.StartEncoder();
		EncoderOn = true;

		app.StartCamera();
		CameraOn = true;

		auto start_time = std::chrono::high_resolution_clock::now();

		for (;;)
		{
			RPiCamEncoder::Msg msg = app.Wait();

			if (msg.type == RPiCamApp::MsgType::Quit)
			{
				TryCameraOff();
				TryEncoderOff();

				{	
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
					TryCameraOff();
					TryEncoderOff();
					
					{
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
				TryCameraOff();
				TryEncoderOff();
				
				{
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
			if ((now - start_time) > options->timeout.value)
			{
				TryCameraOff();
				TryEncoderOff();
				
				{
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
				TryCameraOff();
				TryEncoderOff();
				
				{
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
		TryCameraOff();
		TryEncoderOff();
		
		{
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
			setGeometry(finalcut::FPoint{22,8}, finalcut::FSize{12,1});
			
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
			setGeometry(finalcut::FPoint{23,8}, finalcut::FSize{3,1});
			
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
			setGeometry(finalcut::FPoint{32,8}, finalcut::FSize{3,1});

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
			int minutes{(total_seconds.count() % 3600) / 60};
			int seconds{total_seconds.count() % 60};
			
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
			
			setGeometry (finalcut::FPoint{x,y}, finalcut::FSize{56,15});	

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
				stop_camera.store(true);

				status.setText(finalcut::FString("Stopping..."));

				status.redraw();
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
					status.setText(
						finalcut::FString("Video saved as: ") + 
						finalcut::FString(std_filename)
						);
					break;
				case StopType::TIMEOUT: // timeout
					status.setText(
						finalcut::FString("MAX DURATION. Video saved as: ") +
						finalcut::FString(std_filename)
						);
					break;
				case StopType::ERROR: // video error
					if (info.error_message.empty()) {
						status.setText(finalcut::FString("ERROR: Recording failed"));
						// Recording failed, but no error message (give generic)
					} else {
						status.setText(
							finalcut::FString("ERROR: ") +
							finalcut::FString(info.error_message)
							);
						// Recording failed, send message w/ error
					}
					break;
				default:
					status.setText(finalcut::FString("ERROR: Unknown stop reason"));
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
