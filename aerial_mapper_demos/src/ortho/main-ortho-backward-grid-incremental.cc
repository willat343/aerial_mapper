/*
 *    Filename: main-ortho-backward-grid-incremental.cc
 *  Created on: Jun 25, 2017
 *      Author: Timo Hinzmann
 *   Institute: ETH Zurich, Autonomous Systems Lab
 */

// SYSTEM
#include <memory>

// NON-SYSTEM
#include <aerial-mapper-dense-pcl/stereo.h>
#include <aerial-mapper-dsm/dsm.h>
#include <aerial-mapper-grid-map/aerial-mapper-grid-map.h>
#include <aerial-mapper-io/aerial-mapper-io.h>
#include <aerial-mapper-ortho/ortho-backward-grid.h>
#include <gflags/gflags.h>
#include <ros/ros.h>

DEFINE_string(backward_grid_data_directory, "",
              "Directory to poses, images, and calibration file.");
DEFINE_string(backward_grid_filename_poses, "",
              "Name of the file that contains positions and orientations for "
              "every camera in the global/world frame, i.e. T_G_B");
DEFINE_string(backward_grid_pose_format, "Standard",
              "Format of the pose file. Options: Standard, StandardNamed, COLMAP, "
              "PIX4D, ROS)");
DEFINE_string(backward_grid_prefix_images, "",
              "Prefix of the images to be loaded, e.g. 'images_'");
DEFINE_bool(backward_grid_show_images, false, "Show images when loaded.");
DEFINE_string(
    backward_grid_filename_camera_rig, "",
    "Name of the camera calibration file (intrinsics). File ending: .yaml");
DEFINE_double(backward_grid_center_easting, 0.0,
              "Center [m] of the grid_map (easting).");
DEFINE_double(backward_grid_center_northing, 0.0,
              "Center [m] of the grid_map (northing).");
DEFINE_double(backward_grid_delta_easting, 100.0,
              "Width [m] of the grid_map, starting from center.");
DEFINE_double(backward_grid_delta_northing, 100.0,
              "Height [m] of the grid_map, starting from center");
DEFINE_double(backward_grid_resolution, 1.0, "Resolution of the grid_map [m].");
DEFINE_bool(backward_grid_show_orthomosaic_opencv, true,
            "Show the orthomosaic using opencv?");
DEFINE_bool(backward_grid_save_orthomosaic_jpg, true,
            "Save the orthomosaic as jpg to file?");
DEFINE_string(backward_grid_orthomosaic_jpg_filename, "",
              "Name of the output image storing the orthomsaic.");
DEFINE_double(backward_grid_orthomosaic_elevation_m, 0.0,
              "Height of the orthomosaic if flat ground assumption is used.");
DEFINE_bool(backward_grid_use_digital_elevation_map, true,
            "Use the digital elevation map for generating the orthomosaic? "
            "Otherwise use flat ground assumption.");
DEFINE_string(point_cloud_filename, "",
              "Name of the file that contains the point cloud. If string is "
              "empty, the point cloud is generated from the provided images, "
              "camera poses, camera intrinsics");
DEFINE_int32(dense_pcl_use_every_nth_image, 1,
             "Only use every n-th image in the densification process.");
DEFINE_bool(backward_grid_colored_ortho, false,
            "Generate a colored (RGB) orthomosaic? Otherwise: grayscale.");
DEFINE_bool(backward_grid_use_multi_threads, false,
            "Use multi threads for orthomosaic generation?");
DEFINE_bool(use_BM, true,
            "Use BM Blockmatching if true. Use SGBM (=Semi-Global-) "
            "Blockmatching if false.");

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InstallFailureSignalHandler();

  ros::init(argc, argv, "ortho_backward_grid_incremental");
  ros::Time::init();

  // Parse input parameters.
  const std::string& base = FLAGS_backward_grid_data_directory;
  const std::string& filename_camera_rig =
      FLAGS_backward_grid_filename_camera_rig;
  const std::string& filename_poses = FLAGS_backward_grid_filename_poses;
  const std::string& filename_images = base + FLAGS_backward_grid_prefix_images;

  // Load camera rig from file.
  io::AerialMapperIO io_handler;
  const std::string& filename_camera_rig_yaml = base + filename_camera_rig;
  std::shared_ptr<aslam::NCamera> ncameras =
      io_handler.loadCameraRigFromFile(filename_camera_rig_yaml);
  CHECK(ncameras);

  // Load body poses from file.
  Poses T_G_Bs;
  const std::string& path_filename_poses = base + filename_poses;
  io::PoseFormat pose_format = io::to_format(FLAGS_backward_grid_pose_format);
  std::vector<std::string> image_names;
  io_handler.loadPosesFromFile(pose_format, path_filename_poses, &T_G_Bs, &image_names);

  // Load images from file.
  size_t num_poses = T_G_Bs.size();
  Images images;
  if (image_names.empty()) {
    io_handler.loadImagesFromFile(filename_images, num_poses, &images,
                                  FLAGS_backward_grid_colored_ortho,
                                  FLAGS_backward_grid_show_images);
  } else {
    io_handler.loadImagesFromFile(base, image_names, &images,
                                  FLAGS_backward_grid_colored_ortho,
                                  FLAGS_backward_grid_show_images);
  }

  // Set up layered map (grid_map).
  grid_map::Settings settings_aerial_grid_map;
  settings_aerial_grid_map.center_easting = FLAGS_backward_grid_center_easting;
  settings_aerial_grid_map.center_northing =
      FLAGS_backward_grid_center_northing;
  settings_aerial_grid_map.delta_easting = FLAGS_backward_grid_delta_easting;
  settings_aerial_grid_map.delta_northing = FLAGS_backward_grid_delta_northing;
  settings_aerial_grid_map.resolution = FLAGS_backward_grid_resolution;
  grid_map::AerialGridMap map(settings_aerial_grid_map);

  // Set up dense reconstruction.
  stereo::Settings settings_dense_pcl;
  settings_dense_pcl.use_every_nth_image = FLAGS_dense_pcl_use_every_nth_image;
  LOG(INFO) << "Perform dense reconstruction using planar rectification.";
  stereo::BlockMatchingParameters block_matching_params;
  block_matching_params.use_BM = FLAGS_use_BM;
  stereo::Stereo stereo(ncameras, settings_dense_pcl, block_matching_params);

  // Set up digital surface map.
  dsm::Settings settings_dsm;
  settings_dsm.center_easting = settings_aerial_grid_map.center_easting;
  settings_dsm.center_northing = settings_aerial_grid_map.center_northing;
  dsm::Dsm digital_surface_map(settings_dsm, map.getMutable());

  // Set up orthomosaic.
  ortho::Settings settings_ortho;
  settings_ortho.show_orthomosaic_opencv =
      FLAGS_backward_grid_show_orthomosaic_opencv;
  settings_ortho.save_orthomosaic_jpg =
      FLAGS_backward_grid_save_orthomosaic_jpg;
  settings_ortho.orthomosaic_jpg_filename =
      FLAGS_backward_grid_orthomosaic_jpg_filename;
  settings_ortho.orthomosaic_elevation_m =
      FLAGS_backward_grid_orthomosaic_elevation_m;
  settings_ortho.use_digital_elevation_map =
      FLAGS_backward_grid_use_digital_elevation_map;
  settings_ortho.colored_ortho = FLAGS_backward_grid_colored_ortho;
  settings_ortho.use_multi_threads = FLAGS_backward_grid_use_multi_threads;
  ortho::OrthoBackwardGrid mosaic(ncameras, settings_ortho, map.getMutable());

  // Run all modules incrementally.
  Images images_subset;
  Poses T_G_Bs_subset;
  size_t skip = 0u;
  size_t pcl_cnt = 0;
  for (size_t i = 0u; i < images.size(); ++i) {
    images_subset.push_back(images[i]);
    T_G_Bs_subset.push_back(T_G_Bs[i]);
    if (++skip % FLAGS_dense_pcl_use_every_nth_image == 0) {
      LOG(INFO) << "Processing image " << i << " of " << images.size();
      AlignedType<std::vector, Eigen::Vector3d>::type point_cloud;
      stereo.addFrame(T_G_Bs[i], images[i], &point_cloud);

      if (pcl_cnt > 0) {
        LOG(INFO) << "Filling DSM with " << point_cloud.size() << " points";
        digital_surface_map.process(point_cloud, map.getMutable());

        LOG(INFO) << "Updating orthomosaic layer with " << T_G_Bs_subset.size()
                  << " image-pose-pairs";
        mosaic.process(T_G_Bs_subset, images_subset, map.getMutable());

        LOG(INFO) << "Publishing";
        map.publishOnce();
        images_subset.clear();
        T_G_Bs_subset.clear();
      }
      ++pcl_cnt;
    }
  }

  return 0;
}
