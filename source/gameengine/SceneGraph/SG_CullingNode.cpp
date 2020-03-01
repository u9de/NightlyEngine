#include "SG_CullingNode.h"

SG_CullingNode::SG_CullingNode() : m_culled(true)
{
}

SG_BBox &SG_CullingNode::GetAabb()
{
  return m_aabb;
}

const SG_BBox &SG_CullingNode::GetAabb() const
{
  return m_aabb;
}

bool SG_CullingNode::GetCulled() const
{
  return m_culled;
}

void SG_CullingNode::SetCulled(bool culled)
{
  m_culled = culled;
}
