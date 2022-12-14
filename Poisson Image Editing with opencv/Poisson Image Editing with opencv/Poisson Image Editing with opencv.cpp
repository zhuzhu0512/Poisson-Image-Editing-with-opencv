// Poisson Image Editing with opencv.cpp: 定义控制台应用程序的入口点。
//
#include "stdafx.h"
#include <opencv2/opencv.hpp>  
#include <iostream>  

using namespace cv;
using namespace std;

vector <Mat> rgbx_channel, rgby_channel, output;
Mat destinationGradientX, destinationGradientY;
Mat patchGradientX, patchGradientY;
Mat binaryMaskFloat, binaryMaskFloatInverted;
vector <float> filter_X, filter_Y;

//X梯度
void computeGradientX(const Mat &img, Mat &gx)
{
	Mat kernel = Mat::zeros(1, 3, CV_8S);
	kernel.at<char>(0, 2) = 1;
	kernel.at<char>(0, 1) = -1;

	if (img.channels() == 3)
	{
		filter2D(img, gx, CV_32F, kernel);
	}
	else if (img.channels() == 1)
	{
		Mat tmp[3];
		for (int chan = 0; chan < 3; ++chan)
		{
			filter2D(img, tmp[chan], CV_32F, kernel);
		}
		merge(tmp, 3, gx);
	}
}

//Y梯度
void computeGradientY(const Mat &img, Mat &gy)
{
	Mat kernel = Mat::zeros(3, 1, CV_8S);
	kernel.at<char>(2, 0) = 1;
	kernel.at<char>(1, 0) = -1;

	if (img.channels() == 3)
	{
		filter2D(img, gy, CV_32F, kernel);
	}
	else if (img.channels() == 1)
	{
		Mat tmp[3];
		for (int chan = 0; chan < 3; ++chan)
		{
			filter2D(img, tmp[chan], CV_32F, kernel);
		}
		merge(tmp, 3, gy);
	}
}

//X散度
void computeLaplacianX(const Mat &img, Mat &laplacianX)
{
	Mat kernel = Mat::zeros(1, 3, CV_8S);
	kernel.at<char>(0, 0) = -1;
	kernel.at<char>(0, 1) = 1;
	filter2D(img, laplacianX, CV_32F, kernel);
}

//Y散度
void computeLaplacianY(const Mat &img, Mat &laplacianY)
{
	Mat kernel = Mat::zeros(3, 1, CV_8S);
	kernel.at<char>(0, 0) = -1;
	kernel.at<char>(1, 0) = 1;
	filter2D(img, laplacianY, CV_32F, kernel);
}


void dst(const Mat& src, Mat& dest, bool invert)
{
	Mat temp = Mat::zeros(src.rows, 2 * src.cols + 2, CV_32F);

	int flag = invert ? DFT_ROWS + DFT_SCALE + DFT_INVERSE : DFT_ROWS;

	src.copyTo(temp(Rect(1, 0, src.cols, src.rows)));

	for (int j = 0; j < src.rows; ++j)
	{
		float * tempLinePtr = temp.ptr<float>(j);
		const float * srcLinePtr = src.ptr<float>(j);
		for (int i = 0; i < src.cols; ++i)
		{
			tempLinePtr[src.cols + 2 + i] = -srcLinePtr[src.cols - 1 - i];
		}
	}

	Mat planes[] = { temp, Mat::zeros(temp.size(), CV_32F) };
	Mat complex;

	merge(planes, 2, complex);
	dft(complex, complex, flag);
	split(complex, planes);
	temp = Mat::zeros(src.cols, 2 * src.rows + 2, CV_32F);

	for (int j = 0; j < src.cols; ++j)
	{
		float * tempLinePtr = temp.ptr<float>(j);
		for (int i = 0; i < src.rows; ++i)
		{
			float val = planes[1].ptr<float>(i)[j + 1];
			tempLinePtr[i + 1] = val;
			tempLinePtr[temp.cols - 1 - i] = -val;
		}
	}

	Mat planes2[] = { temp, Mat::zeros(temp.size(), CV_32F) };

	merge(planes2, 2, complex);
	dft(complex, complex, flag);
	split(complex, planes2);

	temp = planes2[1].t();
	dest = Mat::zeros(src.size(), CV_32F);
	temp(Rect(0, 1, src.cols, src.rows)).copyTo(dest);
}


//输入：img为背景图像、mod_diff为散度，mod_diff大小不包含img最外围的像素点  
//也就是说矩阵mod_diff的大小为(w-2)*(h-2),并且mod_diff最外围值(散度)为0  
//输出：result  
//这个函数其实功能是快速求解泊松方程的一种方法
void solve(const Mat &img, Mat& mod_diff, Mat &result)
{
	const int w = img.cols;
	const int h = img.rows;

	Mat res;
	dst(mod_diff, res, false);

	for (int j = 0; j < h - 2; j++)
	{
		float * resLinePtr = res.ptr<float>(j);
		for (int i = 0; i < w - 2; i++)
		{
			resLinePtr[i] /= (filter_X[i] + filter_Y[j] - 4);
		}
	}

	dst(res, mod_diff, true);

	unsigned char *  resLinePtr = result.ptr<unsigned char>(0);
	const unsigned char * imgLinePtr = img.ptr<unsigned char>(0);
	const float * interpLinePtr = NULL;

	//first col
	for (int i = 0; i < w; ++i)
		result.ptr<unsigned char>(0)[i] = img.ptr<unsigned char>(0)[i];

	for (int j = 1; j < h - 1; ++j)
	{
		resLinePtr = result.ptr<unsigned char>(j);
		imgLinePtr = img.ptr<unsigned char>(j);
		interpLinePtr = mod_diff.ptr<float>(j - 1);

		//first row
		resLinePtr[0] = imgLinePtr[0];

		for (int i = 1; i < w - 1; ++i)
		{
			//saturate cast is not used here, because it behaves differently from the previous implementation
			//most notable, saturate_cast rounds before truncating, here it's the opposite.
			float value = interpLinePtr[i - 1];
			if (value < 0.)
				resLinePtr[i] = 0;
			else if (value > 255.0)
				resLinePtr[i] = 255;
			else
				resLinePtr[i] = static_cast<unsigned char>(value);
		}

		//last row
		resLinePtr[w - 1] = imgLinePtr[w - 1];
	}

	//last col
	resLinePtr = result.ptr<unsigned char>(h - 1);
	imgLinePtr = img.ptr<unsigned char>(h - 1);
	for (int i = 0; i < w; ++i)
		resLinePtr[i] = imgLinePtr[i];
}

void poissonSolver(const Mat &img, Mat &laplacianX, Mat &laplacianY, Mat &result)
{
	const int w = img.cols;
	const int h = img.rows;

	Mat lap = Mat(img.size(), CV_32FC1);
	//散度  
	lap = laplacianX + laplacianY;

	Mat bound = img.clone();
	//边界修正，把图片最外围的像素点排除在外，不参与泊松重建  
	rectangle(bound, Point(1, 1), Point(img.cols - 2, img.rows - 2), Scalar::all(0), -1);
	Mat boundary_points;
	Laplacian(bound, boundary_points, CV_32F);

	boundary_points = lap - boundary_points;

	Mat mod_diff = boundary_points(Rect(1, 1, w - 2, h - 2));
	//求解
	solve(img, mod_diff, result);
}

void initVariables(const Mat &destination, const Mat &binaryMask)
{
	destinationGradientX = Mat(destination.size(), CV_32FC3);
	destinationGradientY = Mat(destination.size(), CV_32FC3);
	patchGradientX = Mat(destination.size(), CV_32FC3);
	patchGradientY = Mat(destination.size(), CV_32FC3);

	binaryMaskFloat = Mat(binaryMask.size(), CV_32FC1);
	binaryMaskFloatInverted = Mat(binaryMask.size(), CV_32FC1);

	//init of the filters used in the dst
	const int w = destination.cols;
	filter_X.resize(w - 2);
	for (int i = 0; i < w - 2; ++i)
		filter_X[i] = 2.0f * std::cos(static_cast<float>(CV_PI) * (i + 1) / (w - 1));

	const int h = destination.rows;
	filter_Y.resize(h - 2);
	for (int j = 0; j < h - 2; ++j)
		filter_Y[j] = 2.0f * std::cos(static_cast<float>(CV_PI) * (j + 1) / (h - 1));
}

void computeDerivatives(const Mat& destination, const Mat &patch, const Mat &binaryMask)
{
	initVariables(destination, binaryMask);

	computeGradientX(destination, destinationGradientX);
	computeGradientY(destination, destinationGradientY);

	computeGradientX(patch, patchGradientX);
	computeGradientY(patch, patchGradientY);

	Mat Kernel(Size(3, 3), CV_8UC1);
	Kernel.setTo(Scalar(1));
	erode(binaryMask, binaryMask, Kernel, Point(-1, -1), 3);

	binaryMask.convertTo(binaryMaskFloat, CV_32FC1, 1.0 / 255.0);
}

const void arrayProduct(const cv::Mat& lhs, const cv::Mat& rhs, cv::Mat& result)
{
	vector <Mat> lhs_channels;
	vector <Mat> result_channels;
	//拆分成3个通道的矩阵
	split(lhs, lhs_channels);
	split(result, result_channels);

	//三个矩阵进行分别相乘
	for (int chan = 0; chan < 3; ++chan)
		multiply(lhs_channels[chan], rhs, result_channels[chan]);

	//合成
	merge(result_channels, result);
}


void normalClone(const Mat &destination, const Mat &patch, const Mat &binaryMask, Mat &cloned)
{
	const int w = destination.cols;
	const int h = destination.rows;
	const int channel = destination.channels();
	const int n_elem_in_line = w * channel;

	//变量初始化
   	initVariables(destination, binaryMask);
	//计算梯度
	computeGradientX(destination, destinationGradientX);
	computeGradientY(destination, destinationGradientY);
	computeGradientX(patch, patchGradientX);
	computeGradientY(patch, patchGradientY);
	Mat Kernel(Size(3, 3), CV_8UC1);
	Kernel.setTo(Scalar(1));
	erode(binaryMask, binaryMask, Kernel, Point(-1, -1), 3);
	//将整形图像转为浮点
	binaryMask.convertTo(binaryMaskFloat, CV_32FC1, 1.0 / 255.0);
	bitwise_not(binaryMask, binaryMask);
	binaryMask.convertTo(binaryMaskFloatInverted, CV_32FC1, 1.0 / 255.0);
	//mask操作
	arrayProduct(patchGradientX, binaryMaskFloat, patchGradientX);
	arrayProduct(patchGradientY, binaryMaskFloat, patchGradientY);
	arrayProduct(destinationGradientX, binaryMaskFloatInverted, destinationGradientX);
	arrayProduct(destinationGradientY, binaryMaskFloatInverted, destinationGradientY);
	//计算融合图像的梯度场
	Mat laplacianX = Mat(destination.size(), CV_32FC3);
	Mat laplacianY = Mat(destination.size(), CV_32FC3);
	laplacianX = destinationGradientX + patchGradientX;
	laplacianY = destinationGradientY + patchGradientY;
	namedWindow("GradientX", CV_WINDOW_NORMAL);
	imshow("GradientX", laplacianX);
	resizeWindow("GradientX", (int)laplacianX.cols / 3, (int)laplacianX.rows / 3);
	waitKey(0);
	namedWindow("GradientY", CV_WINDOW_NORMAL);
	imshow("GradientY", laplacianY);
	resizeWindow("GradientY", (int)laplacianY.cols / 3, (int)laplacianY.rows / 3);
	waitKey(0);
	 
	//求解散度
 	computeLaplacianX(laplacianX, laplacianX);
	computeLaplacianY(laplacianY, laplacianY);
	namedWindow("laplacianX", CV_WINDOW_NORMAL);
	imshow("laplacianX", laplacianX);
	resizeWindow("laplacianX", (int)laplacianX.cols / 3, (int)laplacianX.rows / 3);
	waitKey(0);
	namedWindow("laplacianY", CV_WINDOW_NORMAL);
	imshow("laplacianY", laplacianY);
	resizeWindow("laplacianY", (int)laplacianY.cols / 3, (int)laplacianY.rows / 3);
	waitKey(0);

	//三通道分离
	split(laplacianX, rgbx_channel);
	split(laplacianY, rgby_channel);
	split(destination, output);

	//分别求解系数矩阵
	for (int chan = 0; chan < 3; ++chan)
	{
		poissonSolver(output[chan], rgbx_channel[chan], rgby_channel[chan], output[chan]);
	}

	//三通道融合
	merge(output, cloned);
}


void seamlessClone(InputArray _src, InputArray _dst, InputArray _mask, Point p, OutputArray _blend)
{
	const Mat src = _src.getMat();
	const Mat dest = _dst.getMat();
	const Mat mask = _mask.getMat();
	_blend.create(dest.size(), CV_8UC3);//融合后图片的大小肯定跟背景图像一样  
	Mat blend = _blend.getMat();

	int minx = INT_MAX, miny = INT_MAX, maxx = INT_MIN, maxy = INT_MIN;
	int h = mask.size().height;
	int w = mask.size().width;

	Mat gray = Mat(mask.size(), CV_8UC1);
	Mat dst_mask = Mat::zeros(dest.size(), CV_8UC1);//背景图像的mask  
	Mat cs_mask = Mat::zeros(src.size(), CV_8UC3);
	Mat cd_mask = Mat::zeros(dest.size(), CV_8UC3);

	if (mask.channels() == 3)//如果给定的mask是彩色图 需要转换成单通道灰度图  
		cvtColor(mask, gray, COLOR_BGR2GRAY);
	else
		gray = mask;
	//计算包含mask的最小矩形，也就是把那只熊包含起来的最小矩形框，这个矩形是位于src的，后面还有一个对应的矩形位于dst  
	for (int i = 0; i<h; i++)
	{
		for (int j = 0; j<w; j++)
		{

			if (gray.at<uchar>(i, j) == 255)
			{
				minx = min(minx, i);
				maxx = max(maxx, i);
				miny = min(miny, j);
				maxy = max(maxy, j);
			}
		}
	}
	int lenx = maxx - minx;//计算矩形的宽  
	int leny = maxy - miny;//计算矩形的高  


	Mat patch = Mat::zeros(Size(leny, lenx), CV_8UC3);//根据上面的矩形区域，创建一个大小相同矩阵  

	int minxd = p.y - lenx / 2;//计算dst的矩形  
	int maxxd = p.y + lenx / 2;
	int minyd = p.x - leny / 2;
	int maxyd = p.x + leny / 2;

	CV_Assert(minxd >= 0 && minyd >= 0 && maxxd <= dest.rows && maxyd <= dest.cols);

	Rect roi_d(minyd, minxd, leny, lenx);//dst 兴趣区域的矩形  
	Rect roi_s(miny, minx, leny, lenx);//src 兴趣区域矩形  

	Mat destinationROI = dst_mask(roi_d);
	Mat sourceROI = cs_mask(roi_s);

	gray(roi_s).copyTo(destinationROI);//  
	src(roi_s).copyTo(sourceROI, gray(roi_s));
	src(roi_s).copyTo(patch, gray(roi_s));//patch为  

	destinationROI = cd_mask(roi_d);
	cs_mask(roi_s).copyTo(destinationROI);//cs_mask为把前景图片的矩形区域图像 转换到背景图片矩形中的图片  


	normalClone(dest, cd_mask, dst_mask, blend);

}

int main(int argc, char* argv[])
{
	//读取一张图片  
	Mat src = imread("source.jpg");
	if (src.empty())
	{
		printf("Could not load source image.\n");
		waitKey(0);
		return -1;
	}
	//创建一个窗口，设置大小为自动大小  
	namedWindow("Source image", CV_WINDOW_NORMAL);
	//显示该窗口  
	imshow("Source image", src);
	resizeWindow("Source image", (int)src.cols / 3, (int)src.rows / 3);
	waitKey(0);

	Mat mask = imread("mask.jpg");
	if (mask.empty())
	{
		printf("Could not load mask image.\n");
		waitKey(0);
		return -1;
	}
	//创建一个窗口，设置大小为自动大小  
	namedWindow("Mask image", CV_WINDOW_NORMAL);
	//显示该窗口  
	imshow("Mask image", mask);
	resizeWindow("Mask image", (int)mask.cols / 3, (int)mask.rows / 3);
	waitKey(0);

	Mat dst = imread("destination.jpg");
	if (dst.empty())
	{
		printf("Could not load destination image.\n");
		waitKey(0);
		return -1;
	}
	//创建一个窗口，设置大小为自动大小  
	namedWindow("Destination image", CV_WINDOW_NORMAL);
	//显示该窗口  
	imshow("Destination image", dst);
	resizeWindow("Destination image", (int)dst.cols / 3, (int)dst.rows / 3);
	waitKey(0);
	
	Mat blend;
	//Point p(800,600);
	Point p(1800, 350);
	seamlessClone(src, dst, mask, p, blend);
	if (blend.empty())
	{
		printf("Blend image is not exist.\n");
		waitKey(0);
		return -1;
	}
	//创建一个窗口，设置大小为自动大小  
	namedWindow("Blend image", CV_WINDOW_NORMAL);
	//显示该窗口  
	imshow("Blend image", blend);
	resizeWindow("Blend image", (int)blend.cols / 3, (int)blend.rows / 3);
	imwrite("blend.jpg", blend);
	//等待键盘任意键按下关闭此窗口  
	waitKey(0);
	return 0;
}