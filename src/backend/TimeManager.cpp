#include "TimeManager.hpp"

#include "Game.hpp"
#include "Evaluate.hpp"
#include "Tuning.hpp"

#include <algorithm>

DEFINE_PARAM(TM_MovesLeftMidpoint, 41, 30, 60);
DEFINE_PARAM(TM_MovesLeftSteepness, 213, 150, 260);
DEFINE_PARAM(TM_IdealTimeFactor, 830, 700, 1000);
DEFINE_PARAM(TM_NodesCountScale, 199, 160, 260);
DEFINE_PARAM(TM_NodesCountOffset, 53, 10, 90);
DEFINE_PARAM(TM_StabilityScale, 37, 0, 200);
DEFINE_PARAM(TM_StabilityOffset, 1254, 1000, 2000);
DEFINE_PARAM(TM_ScoreChangeFactorScale, 0, 0, 50);
DEFINE_PARAM(TM_ScoreChangeFactorOffset, 1000, 200, 1000);
DEFINE_PARAM(TM_ScoreChangeFactorMin, 50, 10, 90);
DEFINE_PARAM(TM_ScoreChangeFactorMax, 150, 110, 200);

static float EstimateMovesLeft(const uint32_t moves)
{
    // based on LeelaChessZero
    const float midpoint = static_cast<float>(TM_MovesLeftMidpoint);
    const float steepness = static_cast<float>(TM_MovesLeftSteepness) / 100.0f;
    return midpoint * std::pow(1.0f + 1.5f * std::pow((float)moves / midpoint, steepness), 1.0f / steepness) - (float)moves;
}

void InitTimeManager(const Game& game, const TimeManagerInitData& data, SearchLimits& limits)
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
    }

    // fixed move time
    if (data.moveTime != INT32_MAX)
    {
        limits.idealTimeBase = limits.idealTimeCurrent = TimePoint::FromSeconds(0.001f * data.moveTime);
        limits.maxTime = TimePoint::FromSeconds(0.001f * data.moveTime);
    }
}

void UpdateTimeManager(const TimeManagerUpdateData& data, SearchLimits& limits, TimeManagerState& state)
{
    ASSERT(!data.currResult.empty());
    ASSERT(!data.currResult[0].moves.empty());

    if (!limits.idealTimeBase.IsValid() || data.prevResult.empty() || data.prevResult[0].moves.empty())
        return;

    limits.idealTimeCurrent = limits.idealTimeBase;

    // decrease time if PV move is stable
    {
        // update PV move stability counter
        if (data.prevResult[0].moves.front() == data.currResult[0].moves.front())
            state.stabilityCounter++;
        else
            state.stabilityCounter = 0;

        const double stabilityFactor = static_cast<double>(TM_StabilityScale) / 1000.0;
        const double stabilityOffset = static_cast<double>(TM_StabilityOffset) / 1000.0;
        const double stabilityTimeFactor = stabilityOffset - stabilityFactor * std::min(10u, state.stabilityCounter);
        limits.idealTimeCurrent *= stabilityTimeFactor;
    }

    // decrease time if best move score is stable
    {
        const double scale = static_cast<double>(TM_ScoreChangeFactorScale) / 1000.0;
        const double offset = static_cast<double>(TM_ScoreChangeFactorOffset) / 1000.0;
        const double minFactor = static_cast<double>(TM_ScoreChangeFactorMin) / 1000.0;
        const double maxFactor = static_cast<double>(TM_ScoreChangeFactorMax) / 1000.0;

        const size_t maxDepth = data.histScores.size();
        ASSERT(maxDepth >= 3);
        const ScoreType currScore = data.currResult[0].score;

        int32_t scoreChange = data.histScores[maxDepth - 1] - currScore;
        const double scoreChangeFactor = scoreChange * (scoreChange > 0) * scale + offset;
        limits.idealTimeCurrent *= std::clamp(scoreChangeFactor, minFactor, maxFactor);

#ifndef CONFIGURATION_FINAL
        std::cout << "info string scoreChangeFactor " << scoreChangeFactor << std::endl;
#endif // CONFIGURATION_FINAL
    }

    // decrease time if nodes fraction spent on best move is high
    {
        const double nonBestMoveNodeFraction = 1.0 - data.bestMoveNodeFraction;
        const double scale = static_cast<double>(TM_NodesCountScale) / 100.0;
        const double offset = static_cast<double>(TM_NodesCountOffset) / 100.0;
        const double nodeCountFactor = nonBestMoveNodeFraction * scale + offset;
        limits.idealTimeCurrent *= nodeCountFactor;
    }

#ifndef CONFIGURATION_FINAL
    std::cout << "info string ideal time " << limits.idealTimeCurrent.ToSeconds() * 1000.0f << " ms" << std::endl;
#endif // CONFIGURATION_FINAL
}
