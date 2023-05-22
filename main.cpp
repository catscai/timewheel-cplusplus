#include <iostream>
#include <unordered_map>
#include <cstdio>
#include <functional>
#include <algorithm>
#include <set>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <semaphore.h>
#include <cmath>
#include <map>
#include <vector>
#include <memory>
#include <string>
#include <queue>
#include "timewheel_cpp.h"


using namespace std;

struct Test 
{
	int a;
	string str;
	Test() {
		a = 100;
	}
	~Test() {

	}
};


int main()
{
	TimeWheel tw(1000, 100, 0);
	tw.Start();

	TimeWheel::TaskPtr p1 = nullptr;
	TimeWheel::TaskPtr p2 = nullptr;
	TimeWheel::TaskPtr p3 = nullptr;
	TimeWheel::TaskPtr p4 = nullptr;

	p1 = tw.AddTaskAfter(500, true, [&p1]{
		cout << "once 500 ms" <<  endl;
		cout << "use_count = " << p1.use_count() << endl;
	});

	p2 = tw.AddTaskAfter(1000, false, [&p2]{
		cout << "once 1 s" << " taskID = " << p2->TaskID << endl;
	});

	p3 = tw.AddTaskAfter(3000, false, [&p3]{
		cout << "once 3 s" << " taskID = " << p3->TaskID  << endl;
	});

	p4 = tw.AddTaskAfter(2000, true, [&p4]{
		cout << "circle 2 s" << " taskID = " << p4->TaskID  << endl;
	});

	sleep(10);

	tw.Stop();

	sleep(10);


	cout << "main end" << endl;
	return 0;
}
