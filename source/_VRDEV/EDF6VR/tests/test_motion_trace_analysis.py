import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from analyze_motion_trace import analyze, inverse, transpose, qmul


def fields(row, prefix, matrix):
    for i in range(4):
        for j in range(4):
            row[f'{prefix}{4*i+j}'] = matrix[i][j]


def quaternion(row, prefix, q):
    for key, v in zip(('qx', 'qy', 'qz', 'qw'), q):
        row[prefix+key] = v


def fixture(mismatch=False):
    cameras=[]
    for i in range(50):
        yaw=i*.02;c=math.cos(yaw);s=math.sin(yaw)
        row=dict(qpc=1_000_000+i*16667,id=i+1,sample=i+1,predicted=i*16_666_667,kind=0,valid=1)
        fields(row,'camera',[[c,0,-s,0],[0,1,0,0],[s,0,c,0],[i*.02,1.6,0,1]])
        quaternion(row,'',[0,math.sin(yaw/2),0,math.cos(yaw/2)])
        cameras.append(row)
    frames=[]
    for i in range(4,45):
        old=i-2; row=dict(id=i,beginCamera=i+1,endCamera=i+1,viewReads=1,count=2,
            begin=1_001_000+i*16667,viewAt=1_001_100+i*16667,present=1_004_000+i*16667,
            predicted=i*16_666_667,fov0_2=.8,fov0_3=-.8)
        source=[[cameras[old][f'camera{4*j+k}'] for k in range(4)] for j in range(4)]
        fields(row,'view',transpose(inverse(source)))
        scale=1/math.tan(.8);fields(row,'projection',[[scale,0,0,0],[0,scale,0,0],[0,0,1,0],[0,0,1,0]])
        latest=[0,math.sin(i*.01),0,math.cos(i*.01)]
        actual=[0,math.sin((i if mismatch else old)*.01),0,math.cos((i if mismatch else old)*.01)]
        quaternion(row,'latest_',latest)
        for side,sign in (('Left',-1),('Right',1)):
            cant=[0,math.sin(sign*.03),0,math.cos(sign*.03)]
            quaternion(row,'located'+side+'_',qmul(latest,cant))
            quaternion(row,side.lower()+'_',qmul(actual,cant))
        frames.append(row)
    return cameras,frames


class MotionAnalysis(unittest.TestCase):
    def test_delayed_render_with_correct_pose_is_not_an_error(self):
        summary,matches=analyze(*fixture(False),1_000_000)
        self.assertEqual(summary['matrixConvention'],'inverse_transposed')
        self.assertEqual(summary['matchedNativeFrames'],41)
        self.assertEqual(summary['logicUpdatesBehind']['median'],2)
        self.assertLess(summary['leftPoseErrorDeg']['maximum'],.00001)
        self.assertAlmostEqual(summary['nativeVsSubmittedVerticalFovRatio']['median'],1)

    def test_new_pose_on_old_rendered_image_is_detected(self):
        summary,_=analyze(*fixture(True),1_000_000)
        for eye in ('left','right'):
            self.assertAlmostEqual(summary[eye+'PoseErrorDeg']['median'],math.degrees(.04),places=5)

    def test_unmatched_matrices_do_not_manufacture_pose_error(self):
        cameras,frames=fixture(True)
        for row in frames:
            fields(row,'view',[[0]*4 for _ in range(4)])
        summary,_=analyze(cameras,frames,1_000_000)
        self.assertEqual(summary['matchedNativeFrames'],0)
        self.assertEqual(summary['leftPoseErrorDeg']['n'],0)


if __name__ == '__main__':
    unittest.main()
