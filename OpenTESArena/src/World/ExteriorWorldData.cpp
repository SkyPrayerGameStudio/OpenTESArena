#include "ClimateType.h"
#include "ExteriorWorldData.h"
#include "InteriorWorldData.h"
#include "LocationDefinition.h"
#include "LocationType.h"
#include "LocationUtils.h"
#include "WeatherType.h"
#include "VoxelUtils.h"
#include "WorldType.h"
#include "../Assets/MIFUtils.h"
#include "../Assets/MiscAssets.h"

#include "components/debug/Debug.h"

ExteriorWorldData::InteriorState::InteriorState(InteriorWorldData &&worldData,
	const Int2 &returnVoxel)
	: worldData(std::move(worldData)), returnVoxel(returnVoxel) { }

ExteriorWorldData::ExteriorWorldData(ExteriorLevelData &&levelData, bool isCity)
	: levelData(std::move(levelData))
{
	this->isCity = isCity;
}

ExteriorWorldData::~ExteriorWorldData()
{

}

std::string ExteriorWorldData::generateCityInfName(ClimateType climateType, WeatherType weatherType)
{
	const std::string climateLetter = [climateType]()
	{
		if (climateType == ClimateType::Temperate)
		{
			return "T";
		}
		else if (climateType == ClimateType::Desert)
		{
			return "D";
		}
		else
		{
			return "M";
		}
	}();

	// City/town/village letter. Wilderness is "W".
	const std::string locationLetter = "C";

	const std::string weatherLetter = [climateType, weatherType]()
	{
		if ((weatherType == WeatherType::Clear) ||
			(weatherType == WeatherType::Overcast) ||
			(weatherType == WeatherType::Overcast2))
		{
			return "N";
		}
		else if ((weatherType == WeatherType::Rain) ||
			(weatherType == WeatherType::Rain2))
		{
			return "R";
		}
		else if ((weatherType == WeatherType::Snow) ||
			(weatherType == WeatherType::SnowOvercast) ||
			(weatherType == WeatherType::SnowOvercast2))
		{
			// Deserts can't have snow.
			if (climateType != ClimateType::Desert)
			{
				return "S";
			}
			else
			{
				DebugLogWarning("Deserts do not have snow templates.");
				return "N";
			}
		}
		else
		{
			// Not sure what this letter represents.
			return "W";
		}
	}();

	return climateLetter + locationLetter + weatherLetter + ".INF";
}

std::string ExteriorWorldData::generateWildernessInfName(ClimateType climateType, WeatherType weatherType)
{
	const std::string climateLetter = [climateType]()
	{
		if (climateType == ClimateType::Temperate)
		{
			return "T";
		}
		else if (climateType == ClimateType::Desert)
		{
			return "D";
		}
		else
		{
			return "M";
		}
	}();

	// Wilderness is "W".
	const std::string locationLetter = "W";

	const std::string weatherLetter = [climateType, weatherType]()
	{
		if ((weatherType == WeatherType::Clear) ||
			(weatherType == WeatherType::Overcast) ||
			(weatherType == WeatherType::Overcast2))
		{
			return "N";
		}
		else if ((weatherType == WeatherType::Rain) ||
			(weatherType == WeatherType::Rain2))
		{
			return "R";
		}
		else if ((weatherType == WeatherType::Snow) ||
			(weatherType == WeatherType::SnowOvercast) ||
			(weatherType == WeatherType::SnowOvercast2))
		{
			// Deserts can't have snow.
			if (climateType != ClimateType::Desert)
			{
				return "S";
			}
			else
			{
				DebugLogWarning("Deserts do not have snow templates.");
				return "N";
			}
		}
		else
		{
			// Not sure what this letter represents.
			return "W";
		}
	}();

	return climateLetter + locationLetter + weatherLetter + ".INF";
}

ExteriorWorldData ExteriorWorldData::loadCity(const LocationDefinition &locationDef,
	const ProvinceDefinition &provinceDef, const MIFFile &mif, WeatherType weatherType,
	int currentDay, int starCount, const MiscAssets &miscAssets, TextureManager &textureManager)
{
	const MIFFile::Level &level = mif.getLevels().front();
	const LocationDefinition::CityDefinition &cityDef = locationDef.getCityDefinition();
	const std::string infName = ExteriorWorldData::generateCityInfName(cityDef.climateType, weatherType);

	// Generate level data for the city.
	ExteriorLevelData levelData = ExteriorLevelData::loadCity(
		locationDef, provinceDef, level, weatherType, currentDay, starCount, infName,
		mif.getDepth(), mif.getWidth(), miscAssets, textureManager);

	// Generate world data from the level data.
	const bool isCity = true; // False in wilderness.
	ExteriorWorldData worldData(std::move(levelData), isCity);

	// Convert start points from the old coordinate system to the new one.
	for (const OriginalInt2 &point : mif.getStartPoints())
	{
		const Double2 startPointReal = MIFUtils::convertStartPointToReal(point);
		worldData.startPoints.push_back(VoxelUtils::getTransformedVoxel(startPointReal));
	}

	worldData.mifName = mif.getName();

	return worldData;
}

ExteriorWorldData ExteriorWorldData::loadWilderness(const LocationDefinition &locationDef,
	const ProvinceDefinition &provinceDef, WeatherType weatherType, int currentDay, int starCount,
	const MiscAssets &miscAssets, TextureManager &textureManager)
{
	const LocationDefinition::CityDefinition &cityDef = locationDef.getCityDefinition();
	const std::string infName =
		ExteriorWorldData::generateWildernessInfName(cityDef.climateType, weatherType);

	// Load wilderness data (no starting points to load).
	ExteriorLevelData levelData = ExteriorLevelData::loadWilderness(
		locationDef, provinceDef, weatherType, currentDay, starCount, infName,
		miscAssets, textureManager);

	const bool isCity = false; // False if wilderness.

	// Generate world data from the wilderness data.
	ExteriorWorldData worldData(std::move(levelData), isCity);
	worldData.mifName = "WILD.MIF";

	return worldData;
}

InteriorWorldData *ExteriorWorldData::getInterior() const
{
	return (this->interior.get() != nullptr) ? &this->interior->worldData : nullptr;
}

const std::string &ExteriorWorldData::getMifName() const
{
	return (this->interior.get() != nullptr) ?
		this->interior->worldData.getMifName() : this->mifName;
}

WorldType ExteriorWorldData::getBaseWorldType() const
{
	return this->isCity ? WorldType::City : WorldType::Wilderness;
}

WorldType ExteriorWorldData::getActiveWorldType() const
{
	return (this->interior.get() != nullptr) ?
		WorldType::Interior : this->getBaseWorldType();
}

LevelData &ExteriorWorldData::getActiveLevel()
{
	return (this->interior.get() != nullptr) ?
		this->interior->worldData.getActiveLevel() :
		static_cast<LevelData&>(this->levelData);
}

const LevelData &ExteriorWorldData::getActiveLevel() const
{
	return (this->interior.get() != nullptr) ?
		this->interior->worldData.getActiveLevel() :
		static_cast<const LevelData&>(this->levelData);
}

void ExteriorWorldData::enterInterior(InteriorWorldData &&interior, const Int2 &returnVoxel)
{
	DebugAssert(this->interior.get() == nullptr);
	this->interior = std::make_unique<InteriorState>(std::move(interior), returnVoxel);
}

Int2 ExteriorWorldData::leaveInterior()
{
	DebugAssert(this->interior.get() != nullptr);

	const Int2 returnVoxel = this->interior->returnVoxel;
	this->interior = nullptr;

	return returnVoxel;
}
