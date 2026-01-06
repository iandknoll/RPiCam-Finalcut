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

class MainButton : public finalcut::FButton
{
	//Defining a new class to handle our main button
	//Colon defines inheretance--
	//"MainButton" class will inherit from "finalcut::FButton" class
	//ie, it will possess all the same members (attributes and methods)
	//"public" indicates public members of inherited class remain public
	//(more on what a public member is later)

	public:
		//All subsequent members will be public

		explicit MainButton (finalcut::FWidget* parent = nullptr)
			 : finalcut::FButton{parent}
		{
			//Method with same name as class = "constructor"
			//Constructor is always called when an object of the class is made
			//"explicit" is a function specifier for conversion functions
			//It tells compilers not to allow implicit type conversions.
			//This is mostly a safety measure to prevent wrong method use

			//This takes a pointer to a Finalcut Widget as input--
			//Specifically, the widget that serves as our button's parent
			//If no input is given, a default null pointer is used.

			//Finally, we have the method inherit from FButton--
			//Specifically, an instance of FButton w/ pointer "parent" as input


			//Side Note: A "conversion function" converts one type to another.
			//They're declared w/ "operator", or "explicit" for above effect.
			//Any constructor with one input is a conversion function--
			//Since it shows how to convert that input type to our new class.

			//Setup callbacks (using user-defined function further down)
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
		}

		void initCallbacks()
		{
			//Defines a function for setting up callbacks
			addCallback
			(
				"clicked",			//Callback Signal
				this,				//Instance pointer
				&MainButton::cb_cambutton	//Member method pointer
			);
		}

		void cb_cambutton ()
		{
			//Function for handling what happens when we press our button

			//Change text of  button
			setText("&Stop Video");

			//First, we get a pointer to the parent widget using getParent()
			//Initially, the pointer is type FWidget*, but we need FDialog*
			//This is accomplished via static_cast<finalcut::FDialog*>
			//Finally, we store that pointer as parent_dialog--
			//Using auto to allow the compiller to deduce the correct type
			auto parent_dialog = static_cast<finalcut::Fdialog*>(getParent());

			//Null pointers evaluate as false, so check we got pointer succesfully
			if(parent_dialog)
				//If so, use that pointer to redraw the parent dialog
				parent_dialog->redraw();
				//Side-Note: When trying to access a class method:
				//"->" is for pointers, while "." is for objects
		}
}

auto main (int argc, char* argv[]) -> int
{
	finalcut::FApplication app(argc, argv);

	//Create "dialog" object w/ type finalcut::FDialog, assigning "app" as parent
	//This type is base class for dialog windows.
	finalcut::FDialog dialog(&app);

	//Setup Dialog Box
	dialog.setText ("Reaching Task Camera Control");
	dialog.setGeometry (finalcut::FPoint{25,5}, finalcut::FSize{60,20});  //x,y w,h


	//Get current-date time as suggested file name
	std::string suggestion = filename_time();

	//Create "input" object w/ type finalcut::FLineEdit, assigning "dialog" as parent
	//String at start will be default input
	finalcut::FLineEdit input(suggestion, &dialog);

	//Setup input object (Here FPoint relative to dialog)
	input.setGeometry (finalcut::FPoint{14,2}, finalcut::FSize{30,1});
	input.setLabelText("File Name: ");


	//Create "label" object w/ type finalcut::FLabel, assigning "dialog" as parent
	finalcut::FLabel label("In the future a timer will go here!", &dialog);

	//Setup label object (Here FPoint relative to dialog)
	label.setGeometry(finalcut::FPoint{2,4}, finalcut::FSize{40,1});


	//Create "button" object w/ type finalcut::FButton, assigning "dialog" as parent
	//String at start will be default input
	finalcut::FButton button("StartVideo", &dialog);

	//Setup button object (Here FPoint relative to dialog)
	button.setGeometry(finalcut::FPoint{20,7}, finalcut::FSize{20,1});

	//Create callback for when button is clicked
	button.addCallback
	(
		"clicked",		//Callback Signal
		&cb_cambutton,		//Function Pointer
		std::ref(button),	//First function argument
		std::ref(dialog)	//Second function argument
	);

	//Sets "dialog" as the main widget for the application.
	finalcut::FWidget::setMainWidget(&dialog);

	//Make the widget (and all children) visible.
	dialog.show();

	//Starts the FinalCut main event loop (little confused by the fine details...)
	return app.exec();
}
