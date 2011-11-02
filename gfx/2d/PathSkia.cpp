/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Corporation code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *     Matt Woodrow <mwoodrow@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "PathSkia.h"
#include <math.h>
#include "DrawTargetSkia.h"
#include "Logging.h"
#include "HelpersSkia.h"

namespace mozilla {
namespace gfx {

PathBuilderSkia::PathBuilderSkia(const Matrix& aTransform, const SkPath& aPath, FillRule aFillRule)
  : mPath(aPath)
{
  SkMatrix matrix;
  GfxMatrixToSkiaMatrix(aTransform, matrix);
  mPath.transform(matrix);
  SetFillRule(aFillRule);
}

PathBuilderSkia::PathBuilderSkia(FillRule aFillRule)
{
  SetFillRule(aFillRule);
}

void
PathBuilderSkia::SetFillRule(FillRule aFillRule)
{
  mFillRule = aFillRule;
  if (mFillRule == FILL_WINDING) {
    mPath.setFillType(SkPath::kWinding_FillType);
  } else {
    mPath.setFillType(SkPath::kEvenOdd_FillType);
  }
}

void
PathBuilderSkia::MoveTo(const Point &aPoint)
{
  mPath.moveTo(SkFloatToScalar(aPoint.x), SkFloatToScalar(aPoint.y));
}

void
PathBuilderSkia::LineTo(const Point &aPoint)
{
  if (!mPath.countPoints()) {
    MoveTo(aPoint);
  } else {
    mPath.lineTo(SkFloatToScalar(aPoint.x), SkFloatToScalar(aPoint.y));
  }
}

void
PathBuilderSkia::BezierTo(const Point &aCP1,
                          const Point &aCP2,
                          const Point &aCP3)
{
  if (!mPath.countPoints()) {
    MoveTo(aCP1);
  }
  mPath.cubicTo(SkFloatToScalar(aCP1.x), SkFloatToScalar(aCP1.y),
                SkFloatToScalar(aCP2.x), SkFloatToScalar(aCP2.y),
                SkFloatToScalar(aCP3.x), SkFloatToScalar(aCP3.y));
}

void
PathBuilderSkia::QuadraticBezierTo(const Point &aCP1,
                                   const Point &aCP2)
{
  if (!mPath.countPoints()) {
    MoveTo(aCP1);
  }
  mPath.quadTo(SkFloatToScalar(aCP1.x), SkFloatToScalar(aCP1.y),
               SkFloatToScalar(aCP2.x), SkFloatToScalar(aCP2.y));
}

void
PathBuilderSkia::Close()
{
  mPath.close();
}

void
PathBuilderSkia::Arc(const Point &aOrigin, float aRadius, float aStartAngle,
                     float aEndAngle, bool aAntiClockwise)
{ 
  //TODO: Taken directly from azure canvas, we should share this somewhere
  Point startPoint(aOrigin.x + cos(aStartAngle) * aRadius, 
                   aOrigin.y + sin(aStartAngle) * aRadius); 
  
  LineTo(startPoint); 

  // Clockwise we always sweep from the smaller to the larger angle, ccw 
  // it's vice versa.
  if (!aAntiClockwise && (aEndAngle < aStartAngle)) {
    Float correction = ceil((aStartAngle - aEndAngle) / (2.0f * M_PI));
    aEndAngle += correction * 2.0f * M_PI;
  } else if (aAntiClockwise && (aStartAngle < aEndAngle)) {
    Float correction = ceil((aEndAngle - aStartAngle) / (2.0f * M_PI));
    aStartAngle += correction * 2.0f * M_PI;
  }                                                                                                                                     
  
  // Sweeping more than 2 * pi is a full circle.
  if (!aAntiClockwise && (aEndAngle - aStartAngle > 2 * M_PI)) {
    aEndAngle = aStartAngle + 2.0f * M_PI;
  } else if (aAntiClockwise && (aStartAngle - aEndAngle > 2.0f * M_PI)) {
    aEndAngle = aStartAngle - 2.0f * M_PI;
  }

  // Calculate the total arc we're going to sweep.
  Float arcSweepLeft = fabs(aEndAngle - aStartAngle);
  
  Float sweepDirection = aAntiClockwise ? -1.0f : 1.0f;
  
  Float currentStartAngle = aStartAngle;
  
  while (arcSweepLeft > 0) {
    // We guarantee here the current point is the start point of the next
    // curve segment.
    Float currentEndAngle;
    
    if (arcSweepLeft > M_PI / 2.0f) {
      currentEndAngle = currentStartAngle + M_PI / 2.0f * sweepDirection;                                                               
    } else {
      currentEndAngle = currentStartAngle + arcSweepLeft * sweepDirection;
    }
    
    Point currentStartPoint(aOrigin.x + cos(currentStartAngle) * aRadius,
                            aOrigin.y + sin(currentStartAngle) * aRadius);
    Point currentEndPoint(aOrigin.x + cos(currentEndAngle) * aRadius,
                          aOrigin.y + sin(currentEndAngle) * aRadius);
    
    // Calculate kappa constant for partial curve. The sign of angle in the
    // tangent will actually ensure this is negative for a counter clockwise                                                            
    // sweep, so changing signs later isn't needed.
    Float kappa = (4.0f / 3.0f) * tan((currentEndAngle - currentStartAngle) / 4.0f) * aRadius;
    
    Point tangentStart(-sin(currentStartAngle), cos(currentStartAngle));
    Point cp1 = currentStartPoint;
    cp1 += tangentStart * kappa;
    
    Point revTangentEnd(sin(currentEndAngle), -cos(currentEndAngle));
    Point cp2 = currentEndPoint;
    cp2 += revTangentEnd * kappa;
    
    BezierTo(cp1, cp2, currentEndPoint);
    
    arcSweepLeft -= M_PI / 2.0f;
    currentStartAngle = currentEndAngle;
  }
}

Point
PathBuilderSkia::CurrentPoint() const
{
  int pointCount = mPath.countPoints();
  if (!pointCount) {
    return Point(0, 0);
  }
  SkPoint point = mPath.getPoint(pointCount - 1);
  return Point(SkScalarToFloat(point.fX), SkScalarToFloat(point.fY));
}

TemporaryRef<Path>
PathBuilderSkia::Finish()
{
  RefPtr<PathSkia> path = new PathSkia(mPath, mFillRule);
  return path;
}

TemporaryRef<PathBuilder>
PathSkia::CopyToBuilder(FillRule aFillRule) const
{
  return TransformedCopyToBuilder(Matrix(), aFillRule);
}

TemporaryRef<PathBuilder>
PathSkia::TransformedCopyToBuilder(const Matrix &aTransform, FillRule aFillRule) const
{
  RefPtr<PathBuilderSkia> builder = new PathBuilderSkia(aTransform, mPath, aFillRule);
  return builder;
}

bool
PathSkia::ContainsPoint(const Point &aPoint, const Matrix &aTransform) const
{
  Matrix inverse = aTransform;
  inverse.Invert();
  Point transformed = inverse * aPoint;

  Rect bounds = GetBounds(aTransform);

  if (aPoint.x < bounds.x || aPoint.y < bounds.y ||
      aPoint.x > bounds.XMost() || aPoint.y > bounds.YMost()) {
    return false;
  }

  SkRegion pointRect;
  pointRect.setRect(SkFloatToScalar(transformed.x - 1), SkFloatToScalar(transformed.y - 1), 
                    SkFloatToScalar(transformed.x + 1), SkFloatToScalar(transformed.y + 1));

  SkRegion pathRegion;
  
  return pathRegion.setPath(mPath, pointRect);
}

static Rect SkRectToRect(const SkRect& aBounds)
{
  return Rect(SkScalarToFloat(aBounds.fLeft),
              SkScalarToFloat(aBounds.fTop),
              SkScalarToFloat(aBounds.fRight - aBounds.fLeft),
              SkScalarToFloat(aBounds.fBottom - aBounds.fTop));
}

Rect
PathSkia::GetBounds(const Matrix &aTransform) const
{
  Rect bounds = SkRectToRect(mPath.getBounds());
  return aTransform.TransformBounds(bounds);
}

Rect
PathSkia::GetStrokedBounds(const StrokeOptions &aStrokeOptions,
                           const Matrix &aTransform) const
{
  NS_ASSERTION(false, "GetStrokedBounds not supported yet!");
  return Rect(0, 0, 0, 0);
}

}
}
