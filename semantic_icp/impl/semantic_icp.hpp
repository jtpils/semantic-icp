#ifndef SEMANTIC_ICP_HPP_
#define SEMANTIC_ICP_HPP_

#include <iostream>

#include <ceres/ceres.h>
#include <pcl/point_cloud.h>
#include <pcl/common/transforms.h>

#include <gicp_cost_functor_autodiff.h>
#include <local_parameterization_se3.h>

namespace semanticicp
{

template <typename PointT, typename SemanticT>
void SemanticIterativeClosestPoint<PointT,SemanticT>::align(SemanticCloudPtr final) {
    Sophus::SE3d init;
    align(final, init);
};

template <typename PointT, typename SemanticT>
void SemanticIterativeClosestPoint<PointT,SemanticT>::align(
        SemanticCloudPtr final, Sophus::SE3d &initTransform) {
    Sophus::SE3d transform(initTransform);
    std::map<SemanticT, Sophus::SE3d> currentTransforms;
    for( SemanticT s: sourceCloud_->semanticLabels)
        currentTransforms[s] = initTransform;
    bool converged = false;
    size_t count = 0;

    while(converged!=true) {
        double mseHigh = 0;
        count++;
        for(SemanticT s:sourceCloud_->semanticLabels) {
            std::cout << "Label: " << s << std::endl;
            if (targetCloud_->labeledPointClouds.find(s) != targetCloud_->labeledPointClouds.end()) {
            typename pcl::PointCloud<PointT>::Ptr transformedSource (new pcl::PointCloud<PointT>());
            transform = Sophus::SE3d(currentTransforms[s]*baseTransformation_);
            Eigen::Matrix4d transMat = transform.matrix();
            pcl::transformPointCloud(*(sourceCloud_->labeledPointClouds[s]),
                                    *transformedSource,
                                    transMat);

            KdTreePtr tree = targetCloud_->labeledKdTrees[s];
            std::vector<int> targetIndx;
            std::vector<float> distSq;

            // Build The Problem
            ceres::Problem problem;

            // Add Sophus SE3 Parameter block with local parametrization
            Sophus::SE3d estTransformation(currentTransforms[s]);
            problem.AddParameterBlock(estTransformation.data(), Sophus::SE3d::num_parameters,
                                      new LocalParameterizationSE3);


            for(int sourceIndx = 0; sourceIndx != transformedSource->size(); sourceIndx++) {
                const PointT &sourcePoint = transformedSource->points[sourceIndx];

                tree->nearestKSearch(sourcePoint, 1, targetIndx, distSq);
                if( distSq[0] < 4 ) {
                    const Eigen::Matrix3d &sourceCov =
                        (sourceCloud_->labeledCovariances[s])->at(sourceIndx);
                    const PointT &targetPoint =
                        (targetCloud_->labeledPointClouds[s])->points[targetIndx[0]];
                    const Eigen::Matrix3d &targetCov =
                        (targetCloud_->labeledCovariances[s])->at(targetIndx[0]);

                    GICPCostFunctorAutoDiff *c= new GICPCostFunctorAutoDiff(sourcePoint,
                                                                           targetPoint,
                                                                           sourceCov,
                                                                           targetCov,
                                                                           baseTransformation_);
                    ceres::CostFunction* cost_function =
                        new ceres::AutoDiffCostFunction<GICPCostFunctorAutoDiff,
                                                        1,
                                                        Sophus::SE3d::num_parameters>(c);

                    problem.AddResidualBlock(cost_function, NULL, estTransformation.data());
                }

            }
            // Sovler Options
            ceres::Solver::Options options;
            //options.gradient_tolerance = 0.1 * Sophus::Constants<double>::epsilon();
            //options.function_tolerance = 0.1 * Sophus::Constants<double>::epsilon();
            options.linear_solver_type = ceres::DENSE_QR;

            // Solve
            ceres::Solver::Summary summary;
            ceres::Solve(options, &problem, &summary);

            // Print Results
            std::cout << "Solution for label: " << s << std::endl;
            std::cout << estTransformation.matrix() << std::endl;
            std::cout << summary.BriefReport() << std::endl;
            double mse = (currentTransforms[s].inverse()*estTransformation).log().squaredNorm();
            std::cout << "transform squared difference: " << mse << std::endl;

            currentTransforms[s] = estTransformation;
            if(mse>mseHigh)
                mseHigh = mse;

            }

        }
        if(mseHigh < 0.01 || count>20)
            converged = true;
    }
    finalTransformation_ = currentTransforms[0];
};


} // namespace semanticicp

#endif //SEMANTIC_ICP_HPP_
