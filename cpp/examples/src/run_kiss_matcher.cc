#include <filesystem>

#include <kiss_matcher/FasterPFH.hpp>
#include <kiss_matcher/GncSolver.hpp>
#include <kiss_matcher/KISSMatcher.hpp>
#include <pcl/io/pcd_io.h>

#include "quatro/quatro_utils.h"

std::vector<Eigen::Vector3f> convertCloudToVec(const pcl::PointCloud<pcl::PointXYZ>& cloud) {
  std::vector<Eigen::Vector3f> vec;
  vec.reserve(cloud.size());
  for (const auto& pt : cloud.points) {
    vec.emplace_back(pt.x, pt.y, pt.z);
  }
  return vec;
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0]
              << " <src_pcd_file> <tgt_pcd_file> <resolution> <yaw_aug_angle>" << std::endl;
    return -1;
  }
  pcl::PointCloud<pcl::PointXYZ>::Ptr src_pcl(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::PointCloud<pcl::PointXYZ>::Ptr tgt_pcl(new pcl::PointCloud<pcl::PointXYZ>);

  const std::string src_path = argv[1];
  const std::string tgt_path = argv[2];
  const float resolution     = std::stof(argv[3]);

  Eigen::Matrix4f yaw_transform = Eigen::Matrix4f::Identity();
  if (argc > 4) {
    float yaw_aug_angle = std::stof(argv[4]);             // Yaw angle in degrees
    float yaw_rad       = yaw_aug_angle * M_PI / 180.0f;  // Convert to radians

    yaw_transform(0, 0) = std::cos(yaw_rad);
    yaw_transform(0, 1) = -std::sin(yaw_rad);
    yaw_transform(1, 0) = std::sin(yaw_rad);
    yaw_transform(1, 1) = std::cos(yaw_rad);
  }

  std::cout << "Source input: " << src_path << "\n";
  std::cout << "Target input: " << tgt_path << "\n";
  int src_load_result = pcl::io::loadPCDFile<pcl::PointXYZ>(src_path, *src_pcl);
  int tgt_load_result = pcl::io::loadPCDFile<pcl::PointXYZ>(tgt_path, *tgt_pcl);

  pcl::PointCloud<pcl::PointXYZ>::Ptr rotated_src_pcl(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::transformPointCloud(*src_pcl, *rotated_src_pcl, yaw_transform);
  src_pcl = rotated_src_pcl;

  const auto& src_vec = convertCloudToVec(*src_pcl);
  const auto& tgt_vec = convertCloudToVec(*tgt_pcl);

  std::cout << "\033[1;32mLoad complete!\033[0m\n";

  kiss_matcher::KISSMatcherConfig config = kiss_matcher::KISSMatcherConfig(resolution);
  // NOTE(hlim):
  // If the rotation is predominantly around the yaw axis, set `use_quatro_` to true.
  // Otherwise, set `false`; then, the SO(3)-based GNC will be activated.
  // e.g., in case of `VBR-Collosseo`, it should be set as `false`.
  config.use_quatro_ = true;

  // NOTE(hlim):
  // If dealing with a map-level point cloud, setting `use_ratio_test_` to true helps
  // reject redundant features in advance, reducing matching time and filtering outliers early.
  config.use_ratio_test_ = true;
  kiss_matcher::KISSMatcher matcher(config);

  const auto solution = matcher.estimate(src_vec, tgt_vec);

  // Visualization
  pcl::PointCloud<pcl::PointXYZ> src_viz = *src_pcl;
  pcl::PointCloud<pcl::PointXYZ> tgt_viz = *tgt_pcl;
  pcl::PointCloud<pcl::PointXYZ> est_viz;

  Eigen::Matrix4f solution_eigen      = Eigen::Matrix4f::Identity();
  solution_eigen.block<3, 3>(0, 0)    = solution.rotation.cast<float>();
  solution_eigen.topRightCorner(3, 1) = solution.translation.cast<float>();

  matcher.print();

  std::cout << solution_eigen << std::endl;
  std::cout << "=====================================" << std::endl;
  pcl::transformPointCloud(src_viz, est_viz, solution_eigen);

  pcl::PointCloud<pcl::PointXYZ>::Ptr est_cloud(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::transformPointCloud(*src_pcl, *est_cloud, solution_eigen);

  // Save warped source cloud
  std::filesystem::path src_file_path(src_path);
  std::string warped_pcd_filename =
      src_file_path.parent_path().string() + "/" + src_file_path.stem().string() + "_warped.pcd";
  pcl::io::savePCDFileASCII(warped_pcd_filename, *est_cloud);
  std::cout << "Saved transformed source point cloud to: " << warped_pcd_filename << std::endl;

  // Visualization
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr src_colored(new pcl::PointCloud<pcl::PointXYZRGB>);
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr tgt_colored(new pcl::PointCloud<pcl::PointXYZRGB>);
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr est_q_colored(new pcl::PointCloud<pcl::PointXYZRGB>);

  colorize(src_viz, *src_colored, {195, 195, 195});
  colorize(tgt_viz, *tgt_colored, {89, 167, 230});
  colorize(est_viz, *est_q_colored, {238, 160, 61});

  pcl::visualization::PCLVisualizer viewer1("Simple Cloud Viewer");
  viewer1.addPointCloud<pcl::PointXYZRGB>(src_colored, "src_red");
  viewer1.addPointCloud<pcl::PointXYZRGB>(tgt_colored, "tgt_green");
  viewer1.addPointCloud<pcl::PointXYZRGB>(est_q_colored, "est_q_blue");

  while (!viewer1.wasStopped()) {
    viewer1.spinOnce();
  }
}
