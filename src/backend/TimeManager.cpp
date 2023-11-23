#include "TimeManager.hpp"

#include "Game.hpp"
#include "Evaluate.hpp"
#include "Tuning.hpp"

#include <algorithm>

DEFINE_PARAM(TM_MovesLeftMidpoint, 47);
DEFINE_PARAM(TM_MovesLeftSteepness, 205);
DEFINE_PARAM(TM_IdealTimeFactor, 824);
DEFINE_PARAM(TM_NodesCountScaleA, 208); // inc / time = 0
DEFINE_PARAM(TM_NodesCountScaleB, 178); // inc / time = 0.1
DEFINE_PARAM(TM_NodesCountOffset, 46);

static float EstimateMovesLeft(const uint32_t moves)
{
    // based on LeelaChessZero
    const float midpoint = static_cast<float>(TM_MovesLeftMidpoint);
    const float steepness = static_cast<float>(TM_MovesLeftSteepness) / 100.0f;
    return midpoint * std::pow(1.0f + 1.5f * std::pow((float)moves / midpoint, steepness), 1.0f / steepness) - (float)moves;
}

void TimeManager::Init(const Game& game, const TimeManagerInitData& data, SearchLimits& limits)
{
    const int32_t moveOverhead = data.moveOverhead;
    const float movesLeft = data.movesToGo != UINT32_MAX ? (float)data.movesToGo : EstimateMovesLeft(game.GetPosition().GetMoveCount());

    // soft limit
    if (data.remainingTime != INT32_MAX)
    {
        const float idealTimeFactor = static_cast<float>(TM_IdealTimeFactor) / 1000.0f;
        float idealTime = idealTimeFactor * (data.remainingTime / movesLeft + (float)data.timeIncrement);
        float maxTime = (data.remainingTime - moveOverhead) / sqrtf(movesLeft) + (float)data.timeIncrement;

        const float minMoveTime = 0.00001f;
        const float timeMargin = 0.5f;
        maxTime = std::clamp(maxTime, 0.0f, std::max(minMoveTime, timeMargin * (float)data.remainingTime - moveOverhead));
        idealTime = std::clamp(idealTime, 0.0f, std::max(minMoveTime, timeMargin * (float)data.remainingTime - moveOverhead));

#ifndef CONFIGURATION_FINAL
        std::cout << "info string idealTime=" << idealTime << "ms maxTime=" << maxTime << "ms" << std::endl;
#endif // CONFIGURATION_FINAL

        limits.idealTimeBase = limits.idealTimeCurrent = TimePoint::FromSeconds(0.001f * idealTime);

        // abort search if significantly exceeding ideal allocated time
        limits.maxTime = TimePoint::FromSeconds(0.001f * maxTime);

        // activate root singularity search after some portion of estimated time passed
        limits.rootSingularityTime = TimePoint::FromSeconds(0.001f * idealTime * 0.2f);

        limits.timeIncrementRatio = 0.0f;
    }
    else
    {
        limits.timeIncrementRatio = (float)data.timeIncrement / (float)data.remainingTime;
    }

    // fixed move time
    if (data.moveTime != INT32_MAX)
    {
        limits.idealTimeBase = limits.idealTimeCurrent = TimePoint::FromSeconds(0.001f * data.moveTime);
        limits.maxTime = TimePoint::FromSeconds(0.001f * data.moveTime);
    }
}

void TimeManager::Update(const TimeManagerUpdateData& data, SearchLimits& limits)
{
    if (!limits.idealTimeBase.IsValid())
    {
        return;
    }

    // don't update TM at low depths
    if (data.depth < 5)
    {
        return;
    }

    // decrease time if nodes fraction spent on best move is high
    const double nonBestMoveNodeFraction = 1.0 - data.bestMoveNodeFraction;
    const double scaleA = static_cast<double>(TM_NodesCountScaleA) / 100.0;
    const double scaleB = static_cast<double>(TM_NodesCountScaleB) / 100.0;
    const double scale = std::lerp(scaleA, scaleB, 10.0 * std::min(0.1f, limits.timeIncrementRatio));
    const double offset = static_cast<double>(TM_NodesCountOffset) / 100.0;
    const double nodeCountFactor = nonBestMoveNodeFraction * scale + offset;

    // decrease time if best move is not changing
    const double bestMoveStabilityFactor = 1.1 - 0.03 * std::min(12u, data.bestMoveStability);

    // update time
    limits.idealTimeCurrent = limits.idealTimeBase;
    limits.idealTimeCurrent *= nodeCountFactor * bestMoveStabilityFactor;

#ifndef CONFIGURATION_FINAL
    std::cout << "info string nodeCountFactor " << nodeCountFactor << std::endl;
    std::cout << "info string bestMoveStabilityFactor " << bestMoveStabilityFactor << std::endl;
    std::cout << "info string ideal time " << limits.idealTimeCurrent.ToSeconds() * 1000.0f << " ms" << std::endl;
#endif // CONFIGURATION_FINAL
}
