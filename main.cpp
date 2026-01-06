#include <final/final.h> //Includes the basic FinalCut library
#include <ctime> //Need for current date/time
#include <string> //Need to use strings

auto filename_time() -> std::string
{
	//Function to get current date-time as character array

	//Get current time as timestamp object
	std::time_t timestamp = std::time(NULL);

	//Convert timestamp into a datetime struct (aka std::tm)
	//The * before localtime is a dereference operator--
	//localtime returns a pointer-- * gives the object it points to
	//Similarly, & before timestamp is a address-of operator
	//localtime wants a pointer for input-- & gives a pointer for an object
	std::tm datetime = *(std::localtime(&timestamp));

	//Allocate space for our string
	char output[80];

	//Write date-time to our char array
	std::strftime(output, 80, "%m-%d-%y_%H-%M-%S.mp4", &datetime);

	return output;
}

class MainDialog : public finalcut::FDialog
{
	//Defining a new class to handle our main dialog box
	//Colon defines inheretance--
	//"MainButton" class will inherit from "finalcut::FButton" class
	//ie, it will possess all the same members (attributes and methods)
	//"public" indicates public members of inherited class remain public
	//(more on what a public member is later)

	public:
		//All subsequent members will be public

		explicit MainDialog (finalcut::FWidget* parent = nullptr)
			 : finalcut::FDialog{parent}
		{
			//Method with same name as class = "constructor"
			//Constructor is always called when an object of the class is made
			//"explicit" is a function specifier for conversion functions
			//It tells compilers not to allow implicit type conversions.
			//This is mostly a safety measure to prevent wrong method use

			//This takes a pointer to a Finalcut Widget as input--
			//Specifically, the widget that serves as our button's parent
			//If no input is given, a default null pointer is used.

			//Finally, we have the method inherit from FDialog--
			//Specifically, an instance of FDialog w/ pointer "parent" as input


			//Side Note: A "conversion function" converts one type to another.
			//They're declared w/ "operator", or "explicit" for above effect.
			//Any constructor with one input is a conversion function--
			//Since it shows how to convert that input type to our new class.

			//Setup function(s) (defined further down)
			initLayout();
		}

	private:
		//All subsequent members will be private

		void initLayout()
		{
			//Defines a function for startup variables
			setText ("Reaching Task Camera Control");
			setGeometry (finalcut::FPoint{25,5}, finalcut::FSize{60,20});	//x,y w,h

			//Run the inheritted classes initLayout (no effect, but good practice)
			finalcut::FDialog::initLayout();
		}
};

class MainButton : public finalcut::FButton
{
	//Defining a new class to handle our main button
	//It will inherit properties from the "finalcut::FButton" class

	public:
		//All subsequent members will be public

		explicit MainButton (finalcut::FWidget* parent = nullptr)
			 : finalcut::FButton{parent}
		{
			//Constructor, as previously discussed

			//Setup function(s) (defined further down)
			initLayout();
		}

		void setFileNameInput(FileName* input)
		{
			//To tie FileName input into button behaviour, we need a seperate function
			//This function is public so we can modify it after creating the object

			//Define variable "fileInput" via input-- a pointer to a "FileName" object
			fileInput = input;

			//Initialize Callbacks-- have to wait until now as they're reliant on fileInput
			initCallbacks();
		}

	private:
		//All subsequent members will be private

		void initLayout()
		{
			//Defines a function for setup variables
			//(Here FPoint is relative to parent dialog) (x,y w,h)
			setText ("Start Video");
			setGeometry(finalcut::FPoint{20,7}, finalcut::FSize{20,1});

			//Run the inheritted classes initLayout (no effect, but good practice)
			finalcut::FButton::initLayout();
		}

		void initCallbacks()
		{
			//Defines a function for setting up callbacks
			addCallback
			(
				"clicked",			//Callback Signal
				this,				//Instance pointer
				&MainButton::cb_cambutton,	//Member method pointer
				std::ref(*fileInput)		//Pass FileName by reference
			);
		}

		void cb_cambutton (FileName& input)
		{
			//Function for handling what happens when we press our button
			//As input, we take a reference to a "FileName" object
			//This enables us to use the current text of it in callback

			//Check button state:
			if (getText() == "Start Video")
			{
				//Our click indicates we want to start recording

				//Get the text from our user input:
				auto filename = input.getText();
			}
			else
			{
				//Our click indicates want to stop recording
			}

			//Change text of  button
			setText("&Stop Video");

			//To update the screen based on our changes:
			//First, we get a pointer to the parent widget using getParent()
			//Initially, the pointer is type FWidget*, but we need FDialog*
			//This is accomplished via static_cast<finalcut::FDialog*>
			//Finally, we store that pointer as parent_dialog--
			//Using auto to allow the compiller to deduce the correct type
			auto parent_dialog = static_cast<finalcut::FDialog*>(getParent());

			//Null pointers evaluate as false, so check we got pointer succesfully
			if(parent_dialog)
			{
				//If so, use that pointer to redraw the parent dialog
				parent_dialog->redraw();
				//Side-Note 1: When trying to access a class method:
				//"->" is for pointers, while "." is for objects

				//Side-Note 2: In C++, one-line if statements do NOT require {}
				//For consistency, safety, and familarity, I always include them
			}
		}
};

class FileName : public finalcut::FLineEdit
{
	//Defining a new class to handle our filename input
	//It will inherit properties from the "finalcut:FlineEdit" class

	public:
		//All subequent members will be public

		explicit FileName (finalcut::FWidget* parent = nullptr)
			 : finalcut::FLineEdit{parent}
		{
			//Constructor, as previously discussed

			//Setup function(s) (defined further down)
			initLayout();
		}

	private:
		//All subsequent members will be private

		void initLayout()
		{
			//Defines a function for setup variables
			//(Here FPoint is relative to parent dialog) (x,y w,h)

			//Get cirrent date-time as suggested file name
			std::string suggestion = filename_time();

			setText (finalcut::FString{suggestion});	//Need to convert type
			setGeometry (finalcut::FPoint{14,2}, finalcut::FSize{30,1});
			setLabelText("File Name: ");

			//Run the inheritted classes initLayout (no effect, but good practice)
			finalcut::FLineEdit::initLayout();
		}
};

class Timer : public finalcut::FLabel
{
	//Defining a new class to handle our timer (or lack thereof)

	public:
		//All subsequent members will be public

		explicit Timer (finalcut::FWidget* parent = nullptr)
			 : finalcut::FLabel{parent}
		{
			//Constructor, as previously discussed

			//Setup function(s) (defined further down)
			initLayout();
		}

	private:
		//All subsequent members will be private

		void initLayout()
		{
			//Defines a function for setup variables
			//(Here FPoint is relative to parent dialog) (x,y w,h)

			setText("[]");
			setGeometry(finalcut::FPoint{20,9}, finalcut::FSize{40,1});

			//Run the inheritted classes initLayout (no effect, but good practice)
			finalcut::FLabel::initLayout();
		}
};


auto main (int argc, char* argv[]) -> int
{
	// Create the main application object, which manages the Finalcut setup
	finalcut::FApplication app(argc, argv);

	//Create object of our custom dialog box class, w "dialog" as instance name
	MainDialog dialog(&app);

	//Create input object of our custom input class, assigining "dialog" as parent
	FileName input(&dialog);

	//Create label object of our custom timer class, assigning "dialog" as parent
	Timer status(&dialog);

	//Create object of our MainButton class, assigning "dialog" as parent
	MainButton filebutton(&dialog);

	//Sets "dialog" as the main widget for the application.
	finalcut::FWidget::setMainWidget(&dialog);

	//Make the widget (and all children) visible.
	dialog.show();

	//Starts the FinalCut main event loop (little confused by the fine details...)
	return app.exec();
}
